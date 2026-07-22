/* Micro-loop — short feedback delay that locks into freeze near max hold. */
#include "microloop.h"
#include "rate_util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define M_SAMPLE_RATE       44100
#define M_MIN_LEN_SAMPLES   4410     /* 100 ms @ 44.1 kHz — above phasing range */
#define M_MAX_LEN_SAMPLES   264600   /* 6 s @ 44.1 kHz — fits 2 bars down to 80 BPM */
#define M_AUTO_FREEZE       0.95f    /* knob threshold for auto-engaged freeze */
#define M_SMOOTH_COEF       0.9989f  /* ~20 ms — for fb / out_gain */
#define M_CROSSFADE_LEN     2048     /* ~46 ms — equal-power crossfade between
                                         old and new read positions when loop_len
                                         changes. No pitch glide, no click. */
#define M_GRAINS            8        /* voices in the freeze drone cloud       */
#define M_FREEZE_MIX_T      0.050f   /* ~50 ms ramp of cloud in/out on freeze  */
#define M_CLOUD_GAIN        0.90f    /* level trim for the overlapped cloud    */

struct microloop_s {
    float *buf_L;
    float *buf_R;
    int    buf_capacity;
    int    write_pos;
    float  hold;       /* 0..1 — both loop length and blend amount */

    /* Rate-scaled time constants (equal the M_* references at 44.1 kHz). */
    int    min_len;
    int    max_len;
    int    crossfade_len;
    float  smooth_c;

    /* Smoothed gains. */
    float  fb_target,       fb_current;
    float  out_gain_target, out_gain_current;

    /* Loop length (read offset, samples). The host drives a continuous tempo-
     * relative target; a length change RE-ANCHORS via an equal-power crossfade
     * between the old and new FIXED read taps — the read pointer never sweeps,
     * so there is no pitch glide (and no click). Same approach as the looper.
     * Equal-power (not linear) because the two taps read decorrelated content
     * at different delays. Changes mid-fade queue, like the looper. */
    int    loop_len_current;     /* active read offset (also the cloud region) */
    int    loop_len_pending;     /* new offset being crossfaded in             */
    int    loop_len_queued;      /* a change requested mid-crossfade           */
    int    has_queued;           /* 1 if loop_len_queued is waiting            */
    int    xfade_remaining;      /* samples left in the active fade (0 = none) */

    /* Dilate: reverse-delay read (two backward heads offset by half the window,
     * Hann-crossfaded). The base delay read crossfades forward<->reverse by
     * reverse_current; the feedback write stays forward. */
    float  reverse_target, reverse_current;
    int    rev_counter;
    int    rev_base[2];

    /* Octave-up shimmer on the held pad (glassy/crystalline). */
    float *shbuf; int shlen, shwidx; float shphase;
    float  shimmer_amt;          /* ramps in as hold approaches freeze */
    float  lp_L, lp_R, lp_a1;    /* held-pad smoothing low-pass (blended by hold) */

    /* Seamless freeze drone: a granular cloud over the frozen buffer so the
     * held sound has no loop seam. Crossfaded in by freeze_mix while frozen. */
    double sr;
    float  freeze_mix;           /* 0..1: linear-loop read → grain cloud */
    float  freeze_mix_a;         /* per-sample ramp coef for freeze_mix  */
    unsigned rng;                /* LCG state for grain randomisation    */
    int    spawn_timer;          /* samples until the next grain spawns  */
    struct {
        float rd;                /* fractional read cursor into buf      */
        int   len, age, active;  /* grain length / progress / live flag  */
        float gL, gR;            /* per-grain stereo pan gains           */
    } grain[M_GRAINS];
};

/* Cheap LCG → [0,1). Deterministic (no Math.random), fine for grain spread. */
static inline float micro_rng01(microloop_t *m) {
    m->rng = m->rng * 1664525u + 1013904223u;
    return (float)(m->rng >> 8) * (1.0f / 16777216.0f);
}

/* Spawn one grain into a free voice, reading from a random spot within the
 * frozen region and playing forward so it always stays inside captured audio. */
static void micro_spawn_grain(microloop_t *m, int write_pos) {
    int gi = -1;
    for (int i = 0; i < M_GRAINS; i++) if (!m->grain[i].active) { gi = i; break; }
    if (gi < 0) return;                       /* all busy — skip this spawn */

    int region = m->loop_len_current;
    if (region < m->min_len) region = m->min_len;

    int len = (int)((0.15f + 0.25f * micro_rng01(m)) * (float)m->sr);  /* 0.15..0.40 s */
    int minoff = len + 64;
    if (minoff > region) { len = region - 64; if (len < 256) len = 256; minoff = len + 64; }
    int maxoff = region; if (maxoff < minoff) maxoff = minoff;
    int off = minoff + (int)((float)(maxoff - minoff) * micro_rng01(m));

    int cap = m->buf_capacity;
    int start = write_pos - off; while (start < 0) start += cap;
    float theta = (0.30f + 0.40f * micro_rng01(m)) * 1.5707963f;   /* keep both chans */

    m->grain[gi].rd = (float)start;
    m->grain[gi].len = len;
    m->grain[gi].age = 0;
    m->grain[gi].active = 1;
    m->grain[gi].gL = cosf(theta);
    m->grain[gi].gR = sinf(theta);

    /* Schedule next spawn so ~3 grains overlap (smooth overlap-add). */
    m->spawn_timer = len / 3; if (m->spawn_timer < 1) m->spawn_timer = 1;
}

/* Advance + sum the grain cloud for one sample. `active` keeps spawning new
 * Hann-windowed grains; their faded edges hide the loop seam. */
static inline void micro_cloud(microloop_t *m, int active, int write_pos,
                               float *out_l, float *out_r) {
    if (active) { if (--m->spawn_timer <= 0) micro_spawn_grain(m, write_pos); }

    int cap = m->buf_capacity;
    float cl = 0.0f, cr = 0.0f;
    for (int gi = 0; gi < M_GRAINS; gi++) {
        if (!m->grain[gi].active) continue;
        int i0 = (int)m->grain[gi].rd; float fr = m->grain[gi].rd - (float)i0;
        int i1 = i0 + 1; if (i1 >= cap) i1 = 0;
        float sL = m->buf_L[i0] + (m->buf_L[i1] - m->buf_L[i0]) * fr;
        float sR = m->buf_R[i0] + (m->buf_R[i1] - m->buf_R[i0]) * fr;
        /* Hann window over the grain's life. */
        float w = 0.5f - 0.5f * cosf(6.2831853f * ((float)m->grain[gi].age + 0.5f)
                                     / (float)m->grain[gi].len);
        cl += sL * w * m->grain[gi].gL;
        cr += sR * w * m->grain[gi].gR;
        m->grain[gi].rd += 1.0f; if (m->grain[gi].rd >= (float)cap) m->grain[gi].rd -= (float)cap;
        if (++m->grain[gi].age >= m->grain[gi].len) m->grain[gi].active = 0;
    }
    *out_l = cl * M_CLOUD_GAIN;
    *out_r = cr * M_CLOUD_GAIN;
}

/* Octave-up pitch shifter: two read heads, sin²/cos² crossfaded. */
static inline float micro_shimmer (microloop_t *m, float in) {
    m->shbuf[m->shwidx] = in;
    m->shphase -= 1.0f; if (m->shphase < 0.0f) m->shphase += (float)m->shlen;
    const float L = (float)m->shlen;
    const float x0 = m->shphase / L;
    float p1 = m->shphase + L * 0.5f; if (p1 >= L) p1 -= L;
    float s0, s1;
    { int bk=(int)m->shphase; float fr=m->shphase-(float)bk; int i=m->shwidx-bk; while(i<0)i+=m->shlen;
      float a=m->shbuf[i%m->shlen], b=m->shbuf[(i+m->shlen-1)%m->shlen]; s0=a+(b-a)*fr; }
    { int bk=(int)p1; float fr=p1-(float)bk; int i=m->shwidx-bk; while(i<0)i+=m->shlen;
      float a=m->shbuf[i%m->shlen], b=m->shbuf[(i+m->shlen-1)%m->shlen]; s1=a+(b-a)*fr; }
    float w0 = sinf(3.14159265f * x0); w0 = w0*w0;
    m->shwidx = (m->shwidx + 1) % m->shlen;
    return s0*w0 + s1*(1.0f - w0);
}

microloop_t* microloop_create(double sample_rate) {
    const double sr = sample_rate > 0.0 ? sample_rate : AMB_REF_SAMPLE_RATE;
    microloop_t *m = (microloop_t*)calloc(1, sizeof(microloop_t));
    if (!m) return NULL;
    m->min_len       = amb_scale_samples(M_MIN_LEN_SAMPLES, sr);
    m->max_len       = amb_scale_samples(M_MAX_LEN_SAMPLES, sr);
    m->crossfade_len = amb_scale_samples(M_CROSSFADE_LEN, sr);
    m->smooth_c      = amb_scale_onepole(M_SMOOTH_COEF, sr);
    m->buf_capacity = m->max_len;
    m->buf_L = (float*)calloc((size_t)m->buf_capacity, sizeof(float));
    m->buf_R = (float*)calloc((size_t)m->buf_capacity, sizeof(float));
    if (!m->buf_L || !m->buf_R) { microloop_destroy(m); return NULL; }
    m->loop_len_current = m->min_len;
    m->loop_len_pending = m->min_len;
    m->loop_len_queued  = m->min_len;
    m->has_queued       = 0;
    m->xfade_remaining  = 0;
    m->hold = 0.0f;

    /* Octave shimmer buffer (~46 ms) + held-pad smoothing low-pass (~4.5 kHz). */
    m->shlen = amb_scale_samples(2048, sr);
    m->shbuf = (float*)calloc((size_t)m->shlen, sizeof(float));
    if (!m->shbuf) { microloop_destroy(m); return NULL; }
    m->shwidx = 0; m->shphase = 0.0f; m->shimmer_amt = 0.0f;
    m->lp_L = m->lp_R = 0.0f;
    m->lp_a1 = 1.0f - expf(-6.2831853f * 4500.0f / (float)sr);

    /* Freeze drone cloud state. */
    m->sr = sr;
    m->freeze_mix = 0.0f;
    m->freeze_mix_a = 1.0f - expf(-1.0f / (M_FREEZE_MIX_T * (float)sr));
    m->rng = 0x1234567u;
    m->spawn_timer = 0;
    for (int i = 0; i < M_GRAINS; i++) m->grain[i].active = 0;
    return m;
}

void microloop_destroy(microloop_t *m) {
    if (!m) return;
    free(m->buf_L);
    free(m->buf_R);
    free(m->shbuf);
    free(m);
}

/* Clear the captured buffer + freeze/shimmer state (RT-safe). Keeps params. */
void microloop_reset(microloop_t *m) {
    if (!m) return;
    memset(m->buf_L, 0, (size_t)m->buf_capacity * sizeof(float));
    memset(m->buf_R, 0, (size_t)m->buf_capacity * sizeof(float));
    memset(m->shbuf, 0, (size_t)m->shlen * sizeof(float));
    m->write_pos = 0;
    m->fb_current = 0.0f;
    m->out_gain_current = 0.0f;
    m->lp_L = m->lp_R = 0.0f;
    m->shwidx = 0; m->shphase = 0.0f;
    m->xfade_remaining = 0; m->has_queued = 0;   /* cancel any in-flight re-anchor */
    m->reverse_current = 0.0f; m->rev_counter = 0;
    m->freeze_mix = 0.0f; m->spawn_timer = 0;
    for (int i = 0; i < M_GRAINS; i++) m->grain[i].active = 0;
}

/* Dilate: 0 = forward output, 1 = reversed (reverse-delay read). Smoothed. */
void microloop_set_reverse(microloop_t *m, float amount_0_1) {
    if (!m) return;
    if (amount_0_1 < 0.0f) amount_0_1 = 0.0f;
    if (amount_0_1 > 1.0f) amount_0_1 = 1.0f;
    m->reverse_target = amount_0_1;
}

/* Event Horizon: decay the active micro-loop window by `factor` (per block) so
 * lowering Horizon empties the buffer over time, not just ducks the output. */
void microloop_leak(microloop_t *m, float factor) {
    if (!m || factor >= 0.99999f) return;
    if (factor < 0.0f) factor = 0.0f;
    const int cap = m->buf_capacity;
    int len = m->loop_len_current; if (len < 1) len = 1; if (len > cap) len = cap;
    int idx = m->write_pos;
    for (int i = 0; i < len; i++) {
        if (--idx < 0) idx += cap;
        m->buf_L[idx] *= factor;
        m->buf_R[idx] *= factor;
    }
}

void microloop_set_hold(microloop_t *m, float hold_0_1) {
    if (!m) return;
    if (hold_0_1 < 0.0f) hold_0_1 = 0.0f;
    if (hold_0_1 > 1.0f) hold_0_1 = 1.0f;
    m->hold = hold_0_1;
    /* Loop length is now set by the host (tempo-synced divisions) via
     * microloop_set_loop_len(); set_hold controls level / freeze / shimmer. */

    /* Gain targets — process() ramps current → target per sample.
     * Low/mid hold: a subtle 0.25×sqrt texture layer under the loop/reverb.
     * Near the top it swells into a prominent HELD PAD so the freeze/crystallize
     * actually sounds like the crystal glyph (held, frozen, pad). */
    float og = 0.25f * sqrtf(hold_0_1);
    if (hold_0_1 > 0.80f) {
        const float f = (hold_0_1 - 0.80f) / 0.20f;          /* 0..1 across 0.80..1.0 */
        const float base = 0.25f * sqrtf(0.80f);
        og = base + (0.34f - base) * f;                       /* gentle swell, not dominant */
    }
    m->out_gain_target = og;
    /* Feedback curve: a moderate floor so even a short delay gives a few
     * musical repeats (not a bare slapback), rising smoothly to long sustain
     * near the top. The old hold*5 slammed to max by hold 0.2, which made short
     * delays a dense reverb-like comb — this stays a delay across the range. */
    m->fb_target = 0.55f + 0.40f * hold_0_1;     /* 0.55 .. 0.95 */

    /* Octave shimmer ramps in over the upper range (crystallize zone). */
    m->shimmer_amt = (hold_0_1 > 0.60f) ? ((hold_0_1 - 0.60f) / 0.40f) * 0.35f : 0.0f;
}

/* Set the micro-loop length in samples (host computes a continuous, tempo-
 * relative length). A change RE-ANCHORS the read tap via an equal-power
 * crossfade in process() (no pitch glide). If a fade is already running, the
 * newest request is queued and applied when it finishes — so a continuous knob
 * sweep chain-fades through lengths without ever sweeping the read pointer. */
void microloop_set_loop_len(microloop_t *m, int len) {
    if (!m) return;
    if (len < m->min_len) len = m->min_len;
    if (len > m->buf_capacity - 1) len = m->buf_capacity - 1;
    if (m->xfade_remaining > 0) {
        if (len != m->loop_len_pending) { m->loop_len_queued = len; m->has_queued = 1; }
        return;
    }
    if (len != m->loop_len_current) {
        m->loop_len_pending = len;
        m->xfade_remaining  = m->crossfade_len;
    }
}

void microloop_process(microloop_t *m,
                      const float *in_l, const float *in_r,
                      float *out_l, float *out_r,
                      int frames) {
    if (!m || frames <= 0) return;
    /* Auto-engage freeze when knob approaches max. */
    /* Audio freeze disabled: the micro-loop ALWAYS keeps capturing input (no
     * buffer freeze) so the top of Satellite stays the same kind of looping, not
     * a frozen drone. The held-pad grain cloud still ramps in via `hold > 0.85`
     * below (so the crystalline character — and the GUI crystal — remain). */
    const int frozen = 0;
    const int buf_capacity = m->buf_capacity;
    int write_pos = m->write_pos;

    /* Smoothed gains — ramp per sample toward target. */
    float fb_curr   = m->fb_current;
    float out_curr  = m->out_gain_current;
    const float fb_t  = m->fb_target;
    const float out_t = m->out_gain_target;
    const float c     = m->smooth_c;
    const float ic    = 1.0f - c;

    /* Held-pad low-pass amount: only the top zone gets smoothed, so short/mid
     * delays stay crisp & discrete (not a darkened, reverb-like smear). */
    float pad_amt = (m->hold - 0.80f) * (1.0f / 0.15f);
    if (pad_amt < 0.0f) pad_amt = 0.0f; if (pad_amt > 1.0f) pad_amt = 1.0f;

    for (int n = 0; n < frames; n++) {
        fb_curr  = c * fb_curr  + ic * fb_t;
        out_curr = c * out_curr + ic * out_t;

        /* Read at the active (fixed) tap. A length change crossfades to the new
         * tap over crossfade_len samples — both taps are at constant offsets, so
         * the read pointer never sweeps and there is no pitch glide. Equal-power
         * (cos/sin) because the two taps read decorrelated content. */
        int ra = write_pos - m->loop_len_current;
        if (ra < 0) ra += buf_capacity;
        float read_L = m->buf_L[ra];
        float read_R = m->buf_R[ra];
        if (m->xfade_remaining > 0) {
            int rb = write_pos - m->loop_len_pending;
            if (rb < 0) rb += buf_capacity;
            const float t  = (float)(m->crossfade_len - m->xfade_remaining)
                           * (1.0f / (float) m->crossfade_len);     /* 0..1 */
            const float gb = sinf(t * 1.5707963f);
            const float ga = cosf(t * 1.5707963f);
            read_L = ga * read_L + gb * m->buf_L[rb];
            read_R = ga * read_R + gb * m->buf_R[rb];
            if (--m->xfade_remaining == 0) {
                m->loop_len_current = m->loop_len_pending;          /* commit */
                if (m->has_queued && m->loop_len_queued != m->loop_len_current) {
                    m->loop_len_pending = m->loop_len_queued;
                    m->xfade_remaining  = m->crossfade_len;         /* chain next */
                }
                m->has_queued = 0;
            }
        }

        /* Dilate: crossfade the delay read to a reverse-delay read (two backward
         * heads offset by half the window, Hann-crossfaded → no seam, pitch kept).
         * Feeds the cloud/pad chain below so the reversed read still crystallises. */
        m->reverse_current = c * m->reverse_current + ic * m->reverse_target;
        if (m->reverse_current > 0.0005f) {
            int L = m->loop_len_current; if (L < 2) L = 2;
            float rL = 0.0f, rR = 0.0f;
            for (int h = 0; h < 2; h++) {
                int ph = m->rev_counter + (h ? L / 2 : 0);
                while (ph >= L) ph -= L;
                if (ph == 0) m->rev_base[h] = write_pos;
                int rabs = m->rev_base[h] - ph;
                while (rabs < 0) rabs += buf_capacity;
                const float gain = 0.5f - 0.5f * cosf(6.2831853f * (float)ph / (float)L);
                rL += gain * m->buf_L[rabs];
                rR += gain * m->buf_R[rabs];
            }
            const float rc = m->reverse_current;
            read_L = (1.0f - rc) * read_L + rc * rL;
            read_R = (1.0f - rc) * read_R + rc * rR;
        }
        { int Lw = m->loop_len_current < 2 ? 2 : m->loop_len_current;
          m->rev_counter++; if (m->rev_counter >= Lw) m->rev_counter = 0; }

        /* Seamless held-pad via the granular cloud. Hann-windowed overlapping
         * grains taper the edges so the sustained pad has no loop seam. Only
         * engaged in the top held-pad zone (hold > 0.85) and fully when frozen
         * — short/medium delays keep the crisp DIRECT read so they stay
         * discrete echoes, not a smeared reverb-like wash. */
        float cloud_target = frozen ? 1.0f : 0.0f;
        if (m->hold > 0.85f) {
            float s = (m->hold - 0.85f) * (1.0f / 0.10f);
            if (s > 1.0f) s = 1.0f;
            if (s > cloud_target) cloud_target = s;
        }
        m->freeze_mix += m->freeze_mix_a * (cloud_target - m->freeze_mix);
        if (m->freeze_mix > 0.0001f) {
            float cl, cr;
            micro_cloud(m, cloud_target > 0.0f, write_pos, &cl, &cr);
            read_L += (cl - read_L) * m->freeze_mix;
            read_R += (cr - read_R) * m->freeze_mix;
        }

        /* Output: ONLY the loop content. Caller mixes in dry passthrough.
         * Held-pad smoothing: a one-pole low-pass blended in by `hold` so the
         * sustained/crystallized pad is smooth (less seam grain) while short
         * echoes stay crisp; then octave shimmer on top for the glassy sheen. */
        float oL = out_curr * read_L;
        float oR = out_curr * read_R;
        m->lp_L += m->lp_a1 * (oL - m->lp_L);
        m->lp_R += m->lp_a1 * (oR - m->lp_R);
        oL += (m->lp_L - oL) * pad_amt;
        oR += (m->lp_R - oR) * pad_amt;
        if (m->shimmer_amt > 0.0f) {
            float sh = micro_shimmer (m, (oL + oR) * 0.5f);
            oL += m->shimmer_amt * sh;
            oR += m->shimmer_amt * sh;
        }
        out_l[n] = oL;
        out_r[n] = oR;

        /* Write input + feedback into buffer unless frozen. */
        if (!frozen) {
            float new_L = in_l[n] + fb_curr * read_L;
            float new_R = in_r[n] + fb_curr * read_R;
            if (new_L >  1.0f) new_L =  1.0f; else if (new_L < -1.0f) new_L = -1.0f;
            if (new_R >  1.0f) new_R =  1.0f; else if (new_R < -1.0f) new_R = -1.0f;
            m->buf_L[write_pos] = new_L;
            m->buf_R[write_pos] = new_R;
        }

        write_pos++;
        if (write_pos >= buf_capacity) write_pos = 0;
    }
    m->write_pos = write_pos;
    m->fb_current = fb_curr;
    m->out_gain_current = out_curr;
}
