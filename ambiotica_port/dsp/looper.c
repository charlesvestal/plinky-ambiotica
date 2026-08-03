/* Rolling-capture stereo looper with feedback. */
#include "looper.h"
#include "drain.h"
#include "hann.h"
#include "fast_math.h"
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
    int    buf_capacity;   /* allocated size - sets the max loop length */
    float  leak_amount;    /* Event Horizon bleed: 0 = keep the loop, 1 = erase it */
    drain_cursor_t drain;  /* sweep cursor, ascending from the read head - see drain.h */
    int    loop_len;       /* active read offset; <= buf_capacity */
    int    write_pos;
    /* Smoothed feedback - abrupt knob changes would otherwise inject a
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
#define LOOPER_LEAK_MIN      0.30f    /* below this the drain is idle and costs nothing */

/* Padé-3 tanh approximation - smooth soft-saturation for the feedback path.
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
    amb_hann_init();
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
    /* Born empty is not declared empty - see drain.h. Without this, a virgin ring reads as
     * "just cleared" (calloc leaves since_clear at 0 too) and the New Phrase refill would take
     * every power-on and scene load at full input gain instead of the instrument's designed
     * slow build at high Layer. */
    drain_init_recorded(&l->drain, buf_capacity_samples);
    return l;
}

void looper_set_loop_len(looper_t *l, int loop_len_samples) {
    if (!l) return;
    if (loop_len_samples < 1) loop_len_samples = 1;
    if (loop_len_samples > l->buf_capacity) loop_len_samples = l->buf_capacity;

    /* If a crossfade is already running, queue the new value - don't
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
/* Hard clear. Memsets the whole ring, so it BLOCKS - ~4 MB of PSRAM from the calling core,
 * which starves core1 over the shared QSPI bus (measured at a 658 ms audio block). Only safe
 * where a stall is acceptable. Live gestures want looper_set_leak instead. */
void looper_clear(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    memset(l->buf_R, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
}

/* Event Horizon. The plugin this port descends from bleeds the captured loop away as Horizon
 * falls rather than clearing it at the bottom - "actively decays the captured loop so
 * lowering Horizon empties the buffer over time (a global let-go / feedback pull), not just
 * ducks the output". No discrete clear, so nothing to click and nothing to hide, and the
 * buffer is genuinely empty by the time the slider bottoms out.
 *
 * The plugin scales the WHOLE loop window once per block, which here would be ~128k
 * read-modify-writes every 2 ms - unaffordable. This applies the same decay a few samples at
 * a time instead, a cursor walking the window ahead of the read head, so every sample is
 * scaled equally often with only a phase offset between them. Measured at ~37 us per block
 * per sample-per-sample, so LEAK_PER_SAMPLE = 2 costs about 75 us and fits the headroom that
 * a 300 us version did not. See drain.h for the cursor - the direction it walks is what
 * decides whether any of this is audible, and the first version got it backwards.
 *
 * amount 0 leaves the loop alone, 1 zeroes what it touches, partial values decay it - so a
 * quick dip thins the loop and holding at the bottom erases it. */
void looper_set_leak(looper_t *l, float amount_0_1) {
    if (!l) return;
    if (amount_0_1 < 0.0f) amount_0_1 = 0.0f;
    if (amount_0_1 > 1.0f) amount_0_1 = 1.0f;
    if (amount_0_1 > LOOPER_LEAK_MIN && l->leak_amount <= LOOPER_LEAK_MIN) drain_restart(&l->drain);
    l->leak_amount = amount_0_1;
}

/* Declare the loop empty, with no memset and no stall: every tap older than this instant now
 * reads silence. Idempotent, and meant to be called every block for as long as Event Horizon
 * is at the bottom - holding there keeps the mark at "now", so nothing plays until the slider
 * comes back up and new audio is recorded. See drain.h. */
void looper_mark_clear(looper_t *l) {
    if (!l) return;
    drain_mark_clear(&l->drain);
}

/* True while the loop's own read returns silence: the buffer has been declared empty and less
 * than one loop_len has been recorded since. Exposed because the CHAIN has to know - the drift
 * regeneration path feeds the reverb wash back into this looper's input, and a buffer that was
 * just cleared must refill from what you play, not from the tail of what you cleared. Same
 * test looper_process itself makes at read_pos_a, so this agrees with what the loop actually
 * sounds like. */
int looper_is_empty(const looper_t *l) {
    return l ? drain_stale(&l->drain, l->loop_len) : 0;
}

void looper_reset(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    memset(l->buf_R, 0, (size_t)l->buf_capacity * sizeof(lsamp_t));
    l->write_pos = 0;
    l->leak_amount = 0.0f;
    l->fb_current = 0.0f;
    l->crossfade_remaining = 0;
    l->has_queued = 0;
    l->reverse_current = 0.0f;
    l->rev_counter = 0;
    l->reverse_was_active = 0;
    /* Same reasoning as looper_create: a reset re-enters the born-empty state, not the
     * declared-empty one, so it must not trigger the New Phrase fast refill either. */
    drain_init_recorded(&l->drain, l->buf_capacity);
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
 * at write_pos) by `factor` once per block - actively decays the captured loop so
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
    /* knob^2 curve for perceptual feel - soft start, lush mid, very long top.
     * Capped below unity so the looper never fully freezes (fb=1 -> (1-fb)=0
     * stops capturing input): at 100% it's a long-sustain loop that still
     * takes new input. Soft-clip in process() guards against runaway. */
    float fb = layer_0_1 * layer_0_1;
    if (fb > 0.97f) fb = 0.97f;
    l->fb_target = fb;
}

void PLINKY_DSP_RAM_FUNC(looper_process)(looper_t *l,
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

        /* Read from active loop_len position. Anything older than the last clear reads as
         * silence and skips the fetch entirely - see drain.h. */
        int read_pos_a = pos - l->loop_len;
        if (read_pos_a < 0) read_pos_a += cap;
        /* Hoisted: the write below needs to know there was nothing to read, not just the
         * read itself. During a loop-length crossfade the PENDING read may disagree, but the
         * active length is the one the write law is balanced against, so this is the one. */
        const int stale_a = drain_stale(&l->drain, l->loop_len);
        float loopL = 0.0f, loopR = 0.0f;
        if (!stale_a) {
            loopL = ld(l->buf_L[read_pos_a]);
            loopR = ld(l->buf_R[read_pos_a]);
        }

        /* During crossfade, blend with read at the pending loop_len.
         * Linear (gain-equal) crossfade - the two read positions are highly
         * correlated (same buffer, shifted by a few samples) so equal-power
         * cosine/sine causes a 3 dB hump mid-fade. Linear stays flat. */
        if (l->crossfade_remaining > 0) {
            int read_pos_b = pos - l->loop_len_pending;
            if (read_pos_b < 0) read_pos_b += cap;
            float loopL_b = 0.0f, loopR_b = 0.0f;
            if (!drain_stale(&l->drain, l->loop_len_pending)) {
                loopL_b = ld(l->buf_L[read_pos_b]);
                loopR_b = ld(l->buf_R[read_pos_b]);
            }

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
         * state buffer content converges to input level - no buildup, no
         * runaway. At fb=1.0 the input term goes to zero, naturally freezing
         * the buffer (true looper). soft_sat kept as safety against
         * transient peaks. */
        /* REFILL AFTER A CLEAR. The normalised law above balances input against what is
         * already in the buffer, so once a clear makes `old` read as silence it also makes
         * the input term the only term - and at Layer 100% that term is 1-0.97 = 0.03. A
         * cleared loop would take ~33 passes to come back to level, which is minutes, and
         * which is exactly what Event Horizon's release used to sound like.
         *
         * So take the input at FULL gain precisely while there is nothing to mix it with.
         * `stale_a` is true for exactly one loop_len after a clear (drain.h), so this
         * self-terminates after one pass and needs no counter of its own.
         *
         * The handover needs no smoothing: at the first non-stale sample `old` is the
         * full-level material the previous pass just wrote, so (1-fb)*in + fb*old still
         * evaluates to about `in`. There is no step to fade. */
        float in_g = stale_a ? 1.0f : (1.0f - fb_curr);
        /* Event Horizon leak. What it does is in looper_set_leak; why the cursor is shaped
           the way it is, and why the rate is 2, is in drain.h. Placed after the read at
           read_pos_a so the tap always plays a sample before the sweep erases it. */
        if (l->leak_amount > LOOPER_LEAK_MIN) {
            const float lf = 1.0f - l->leak_amount;
            const int zeroing = (lf < 0.02f);   /* at the bottom, skip the read entirely */
            int win = l->loop_len; if (win > cap) win = cap; if (win < 1) win = 1;
            for (int k = 0; k < LEAK_PER_SAMPLE; k++) {
                int idx = drain_next(&l->drain, read_pos_a, win, cap);
                if (zeroing) { l->buf_L[idx] = st(0.0f);             l->buf_R[idx] = st(0.0f); }
                else         { l->buf_L[idx] = st(ld(l->buf_L[idx]) * lf);
                               l->buf_R[idx] = st(ld(l->buf_R[idx]) * lf); }
            }
        }
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
         * from a stale anchor at non-zero Hann gain for up to one loop - the seam
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
            const float inv_L = 1.0f / (float)L;   /* hoisted: the divide was per head, per sample */
            float rL = 0.0f, rR = 0.0f;
            for (int h = 0; h < 2; h++) {
                int ph = l->rev_counter + (h ? L / 2 : 0);
                while (ph >= L) ph -= L;
                if (ph == 0) l->rev_base[h] = pos;          /* anchor window start at "now" */
                /* The anchor is already ph behind the write head when it is set and both
                 * advance one per sample, so this head is reading 2*ph back - up to twice
                 * loop_len, further than any tap the sweep window covers. */
                if (drain_stale(&l->drain, 2 * ph)) continue;
                int rabs = l->rev_base[h] - ph;             /* read backward from the anchor */
                while (rabs < 0) rabs += cap;
                const float gain = amb_hann(ph, inv_L);
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

        drain_tick(&l->drain, cap);
        pos++; if (pos >= cap) pos = 0;
    }
    l->write_pos = pos;
    l->fb_current = fb_curr;
}
