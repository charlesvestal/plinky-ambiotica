/* Rolling-capture stereo looper with feedback. */
#include "looper.h"
#include "rate_util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* Loop-bed storage type. Default float; build with -DLOOPER_I16 to store the
 * bed as int16 (Plinky-native): halves the PSRAM footprint and bytes/sample on
 * the slow bus. The loop is sequential-access so int16 adds no scatter penalty;
 * the feedback path picks up ~96 dB quantization noise, inaudible under the
 * soft-saturated ambient wash. ld()/st() are identity in the float build, so
 * that build is behaviourally unchanged. */
#ifdef LOOPER_I16
typedef int16_t lsamp_t;
static inline float   ld(lsamp_t v) { return (float)v * (1.0f / 32768.0f); }
static inline lsamp_t st(float v) {
    v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    /* round-to-nearest without lrintf (wasm toolchain doesn't provide it) */
    float s = v * 32767.0f;
    return (lsamp_t)(s < 0.0f ? s - 0.5f : s + 0.5f);
}
#else
typedef float lsamp_t;
static inline float   ld(lsamp_t v) { return v; }
static inline lsamp_t st(float v) { return v; }
#endif

struct looper_s {
    lsamp_t *buf_L;
    lsamp_t *buf_R;
    int    buf_capacity;   /* allocated size — sets the max loop length */
    int    loop_len;       /* active read offset; <= buf_capacity */
    int    write_pos;
    /* Smoothed feedback — abrupt knob changes would otherwise inject a
     * step into the buffer and echo forever. */
    float  fb_target;
    float  fb_current;

    /* Loop-length crossfade state (same pattern as microloop). MIDI-clock
     * follower can re-anchor loop_len every loop pass; crossfade hides
     * the resulting read-pointer jump. */
    int    loop_len_pending;
    int    loop_len_queued;
    int    has_queued;
    int    crossfade_remaining;

    /* Rate-scaled time constants (equal the references at 44.1 kHz). */
    float  smooth_c;
    int    crossfade_len;

    /* Dilate: reverse-delay read. Two backward heads offset by half the window,
     * Hann-crossfaded so there's no seam click and pitch is preserved. The OUTPUT
     * crossfades forward<->reverse by reverse_current; the feedback write stays
     * forward (the loop keeps evolving normally, we just play it back backward). */
    float  reverse_target, reverse_current;
    int    rev_counter;
    int    rev_base[2];
    int    reverse_was_active;  /* edge-detect reverse onset to re-anchor the read window */
};

#define LOOPER_SMOOTH_COEF   0.9989f  /* ~20 ms time constant @ 44.1 kHz */
#define LOOPER_CROSSFADE_LEN 512      /* ~11.6 ms linear crossfade */

/* Padé-3 tanh approximation — smooth soft-saturation for the feedback path.
 * Cheap (5 muls + 2 adds inside range) and bounded to ±1.0. Replaces hard
 * clip which produced clicky edges when sustained input + fb=1 saturated
 * the buffer. */
static inline float soft_sat(float x) {
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

looper_t* looper_create(int buf_capacity_samples, double sample_rate) {
    const double sr = sample_rate > 0.0 ? sample_rate : AMB_REF_SAMPLE_RATE;
    if (buf_capacity_samples <= 0) return NULL;
    looper_t *l = (looper_t*)calloc(1, sizeof(looper_t));
    if (!l) return NULL;
    l->smooth_c      = amb_scale_onepole(LOOPER_SMOOTH_COEF, sr);
    l->crossfade_len = amb_scale_samples(LOOPER_CROSSFADE_LEN, sr);
    l->buf_capacity = buf_capacity_samples;
    l->loop_len = buf_capacity_samples;   /* default: full capacity */
    l->buf_L = (lsamp_t*)calloc((size_t)buf_capacity_samples, sizeof(lsamp_t));
    l->buf_R = (lsamp_t*)calloc((size_t)buf_capacity_samples, sizeof(lsamp_t));
    if (!l->buf_L || !l->buf_R) { looper_destroy(l); return NULL; }
    return l;
}

void looper_set_loop_len(looper_t *l, int loop_len_samples) {
    if (!l) return;
    if (loop_len_samples < 1) loop_len_samples = 1;
    if (loop_len_samples > l->buf_capacity) loop_len_samples = l->buf_capacity;

    /* If a crossfade is already running, queue the new value — don't
     * disrupt the in-flight transition. */
    if (l->crossfade_remaining > 0) {
        if (loop_len_samples != l->loop_len_pending) {
            l->loop_len_queued = loop_len_samples;
            l->has_queued = 1;
        }
        return;
    }
    if (loop_len_samples != l->loop_len) {
        l->loop_len_pending = loop_len_samples;
        l->crossfade_remaining = l->crossfade_len;
    }
}

void looper_destroy(looper_t *l) {
    if (!l) return;
    free(l->buf_L);
    free(l->buf_R);
    free(l);
}

/* Clear the captured loop + feedback state (RT-safe: no alloc). Keeps params. */
void looper_reset(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    memset(l->buf_R, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    l->write_pos = 0;
    l->fb_current = 0.0f;
    l->crossfade_remaining = 0;
    l->has_queued = 0;
    l->reverse_current = 0.0f;
    l->rev_counter = 0;
    l->reverse_was_active = 0;
}

/* Dilate: 0 = forward output, 1 = reversed (reverse-delay read). Smoothed in
 * process so toggling doesn't click. The feedback path stays forward. */
void looper_set_reverse(looper_t *l, float amount_0_1) {
    if (!l) return;
    if (amount_0_1 < 0.0f) amount_0_1 = 0.0f;
    if (amount_0_1 > 1.0f) amount_0_1 = 1.0f;
    l->reverse_target = amount_0_1;
}

/* Event Horizon: scale the active loop window (the last loop_len samples ending
 * at write_pos) by `factor` once per block — actively decays the captured loop so
 * lowering Horizon empties the buffer over time (a global "let-go"/feedback pull),
 * not just ducks the output. factor >= 1 is a no-op. RT-safe (no alloc). */
void looper_leak(looper_t *l, float factor) {
    if (!l || factor >= 0.99999f) return;
    if (factor < 0.0f) factor = 0.0f;
    const int cap = l->buf_capacity;
    int len = l->loop_len; if (len > cap) len = cap;
    int idx = l->write_pos;
    for (int i = 0; i < len; i++) {
        if (--idx < 0) idx += cap;
        l->buf_L[idx] = st(ld(l->buf_L[idx]) * factor);
        l->buf_R[idx] = st(ld(l->buf_R[idx]) * factor);
    }
}

void looper_set_layer(looper_t *l, float layer_0_1) {
    if (!l) return;
    if (layer_0_1 < 0.0f) layer_0_1 = 0.0f;
    if (layer_0_1 > 1.0f) layer_0_1 = 1.0f;
    /* knob^2 curve for perceptual feel — soft start, lush mid, very long top.
     * Capped below unity so the looper never fully freezes (fb=1 -> (1-fb)=0
     * stops capturing input): at 100% it's a long-sustain loop that still
     * takes new input. Soft-clip in process() guards against runaway. */
    float fb = layer_0_1 * layer_0_1;
    if (fb > 0.97f) fb = 0.97f;
    l->fb_target = fb;
}

void looper_clear(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    memset(l->buf_R, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
}

void looper_process(looper_t *l,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!l || frames <= 0) return;
    int pos = l->write_pos;
    const int cap = l->buf_capacity;

    float fb_curr      = l->fb_current;
    const float fb_t   = l->fb_target;
    const float c      = l->smooth_c;
    const float ic     = 1.0f - c;

    for (int n = 0; n < frames; n++) {
        fb_curr = c * fb_curr + ic * fb_t;

        /* Read from active loop_len position. */
        int read_pos_a = pos - l->loop_len;
        if (read_pos_a < 0) read_pos_a += cap;
        float loopL = ld(l->buf_L[read_pos_a]);
        float loopR = ld(l->buf_R[read_pos_a]);

        /* During crossfade, blend with read at the pending loop_len.
         * Linear (gain-equal) crossfade — the two read positions are highly
         * correlated (same buffer, shifted by a few samples) so equal-power
         * cosine/sine causes a 3 dB hump mid-fade. Linear stays flat. */
        if (l->crossfade_remaining > 0) {
            int read_pos_b = pos - l->loop_len_pending;
            if (read_pos_b < 0) read_pos_b += cap;
            float loopL_b = ld(l->buf_L[read_pos_b]);
            float loopR_b = ld(l->buf_R[read_pos_b]);

            float gain_b = (float)(l->crossfade_len - l->crossfade_remaining) *
                           (1.0f / (float)l->crossfade_len);
            float gain_a = 1.0f - gain_b;
            loopL = gain_a * loopL + gain_b * loopL_b;
            loopR = gain_a * loopR + gain_b * loopR_b;

            l->crossfade_remaining--;
            if (l->crossfade_remaining == 0) {
                l->loop_len = l->loop_len_pending;
                if (l->has_queued && l->loop_len_queued != l->loop_len) {
                    l->loop_len_pending = l->loop_len_queued;
                    l->crossfade_remaining = l->crossfade_len;
                }
                l->has_queued = 0;
            }
        }

        /* Write input + feedback back into the buffer at write_pos.
         * Normalized feedback formula: buf = (1-fb)*in + fb*old. At steady
         * state buffer content converges to input level — no buildup, no
         * runaway. At fb=1.0 the input term goes to zero, naturally freezing
         * the buffer (true looper). soft_sat kept as safety against
         * transient peaks. */
        float in_g = 1.0f - fb_curr;
        l->buf_L[pos] = st(soft_sat(in_g * in_l[n] + fb_curr * loopL));
        l->buf_R[pos] = st(soft_sat(in_g * in_r[n] + fb_curr * loopR));

        /* Output: ONLY the loop signal (no dry). Caller mixes dry separately.
         * Makeup gain: the normalized (1-fb) capture + soft-clip leaves the loop
         * a bit under the dry level at high feedback; lift it so Layer ~100%
         * sits about equal to the dry signal (measured ~0.62x without this). */
        const float mk = 1.0f + 0.6f * fb_curr;

        /* Dilate: crossfade the OUTPUT to a reverse-delay read of the same buffer
         * (two backward heads offset by half the window, Hann-crossfaded → no seam,
         * pitch preserved). Forward feedback above is untouched. */
        float outLoopL = loopL, outLoopR = loopR;
        l->reverse_current = c * l->reverse_current + ic * l->reverse_target;
        const int rev_active = l->reverse_current > 0.0005f;
        /* On reverse ONSET, re-anchor the backward read window to "now". rev_counter
         * free-runs even while reverse is off, so without this the heads would read
         * from a stale anchor at non-zero Hann gain for up to one loop — the seam
         * masking that keeps steady-state reverse clickless doesn't cover turn-on
         * (an audible click when Dilate engages over a loud loop). Start head 0 at
         * phase 0 (gain 0) and both anchors at the write head so the first reads are
         * valid recent audio, blended in smoothly by the reverse_current ramp. */
        if (rev_active && !l->reverse_was_active) {
            l->rev_counter = 0;
            l->rev_base[0] = pos;
            l->rev_base[1] = pos;
        }
        l->reverse_was_active = rev_active;
        if (rev_active) {
            int L = l->loop_len; if (L < 2) L = 2;
            float rL = 0.0f, rR = 0.0f;
            for (int h = 0; h < 2; h++) {
                int ph = l->rev_counter + (h ? L / 2 : 0);
                while (ph >= L) ph -= L;
                if (ph == 0) l->rev_base[h] = pos;          /* anchor window start at "now" */
                int rabs = l->rev_base[h] - ph;             /* read backward from the anchor */
                while (rabs < 0) rabs += cap;
                const float gain = 0.5f - 0.5f * cosf(6.2831853f * (float)ph / (float)L);
                rL += gain * ld(l->buf_L[rabs]);
                rR += gain * ld(l->buf_R[rabs]);
            }
            const float rc = l->reverse_current;
            outLoopL = (1.0f - rc) * loopL + rc * rL;
            outLoopR = (1.0f - rc) * loopR + rc * rR;
        }
        { int Lw = l->loop_len < 2 ? 2 : l->loop_len;
          l->rev_counter++; if (l->rev_counter >= Lw) l->rev_counter = 0; }

        out_l[n] = fb_curr * outLoopL * mk;
        out_r[n] = fb_curr * outLoopR * mk;

        pos++; if (pos >= cap) pos = 0;
    }
    l->write_pos = pos;
    l->fb_current = fb_curr;
}
