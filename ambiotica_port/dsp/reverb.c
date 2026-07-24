/* Ambiotica reverb — 8 parallel damped combs + 4 serial allpasses, with
 * per-comb modulated read positions for a "breathing" lush ambient tail.
 *
 * Each comb's read is offset by a slow sine LFO running at its own rate
 * (asynchronous across the 8 voices), giving the tail an evolving pitch
 * wobble that never repeats. mod_depth = 0 collapses to a static plate.
 */
#include "reverb.h"
#include "lfo.h"
#include "rate_util.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

/* Modal comb reverb. The shipping panel uses the Dattorro plate (AMB_DATTORRO); this
 * is the harness fallback. Lives in fast SRAM (PSRAM scatter was too slow); 4 combs. */
#define R_COMB_MAX       4
#define R_COMB           4
#define R_AP             4
#define R_STEREO_SPREAD  37
#define R_MOD_HEADROOM   256   /* extra samples per comb buffer for mod range */
#define R_SAMPLE_RATE    44100

/* Comb lengths (samples @ 44.1 kHz) span ~50–74 ms — chosen for
 * ambient-pad modal density (longer than typical small-room values). */
static const int R_COMB_BASE[R_COMB_MAX] = {
    2237, 2557, 2861, 3137        /* 4 spread across the original 8 */
};
static const int R_AP_BASE[R_AP] = {
    347, 421, 511, 619
};

/* Initial LFO phase per comb — irregular placement so the 8 mods start
 * decorrelated rather than aligned. Phases drift apart further at runtime
 * because each comb runs at its own rate (see R_COMB_RATE_MULT). */
static const float R_COMB_PHASE[R_COMB_MAX] = {
    0.00f, 1.71f, 3.27f, 5.02f
};

/* Per-comb LFO rate multipliers — each comb runs at base_rate * mult[i].
 * Spread around 1.0 with irrational-ish ratios so the 8 LFOs never re-sync.
 * This is the "asynchronous LFO" character: tail evolves continuously
 * instead of pulsing in unison (= detune sound). */
static const float R_COMB_RATE_MULT[R_COMB_MAX] = {
    1.000f, 0.872f, 0.954f, 0.827f
};

struct reverb_s {
    /* L channel — combs */
    float *comb_buf_L[R_COMB_MAX];
    int    comb_buf_len_L[R_COMB_MAX];
    int    comb_write_L[R_COMB_MAX];
    float  comb_damp_L[R_COMB_MAX];

    float *ap_buf_L[R_AP];
    int    ap_buf_len_L[R_AP];
    int    ap_pos_L[R_AP];

    /* R channel — combs (length includes stereo spread offset) */
    float *comb_buf_R[R_COMB_MAX];
    int    comb_buf_len_R[R_COMB_MAX];
    int    comb_write_R[R_COMB_MAX];
    float  comb_damp_R[R_COMB_MAX];

    float *ap_buf_R[R_AP];
    int    ap_buf_len_R[R_AP];
    int    ap_pos_R[R_AP];

    /* Rate-scaled geometry (computed from sample_rate in reverb_create).
     * At 44.1 kHz these equal the R_*_BASE / R_STEREO_SPREAD reference
     * constants exactly. */
    int   comb_base[R_COMB_MAX];
    int   ap_base[R_AP];
    int   stereo_spread;
    float mod_depth_max;       /* scaled equivalent of the 45-sample max swing */
    float hp_a;                /* scaled input-HPF one-pole coef */
    float smooth_c;            /* scaled ~20 ms smoothing coef */

    /* Tunables */
    float fb_target,           fb_current;
    float damp_a;
    float ap_g;
    float input_gain;
    float wet_gain;

    /* Modulation — one LFO per comb, each at its own rate. */
    lfo_t lfo[R_COMB_MAX];
    float base_rate_hz;       /* user-set rate before per-comb multiplication */
    float mod_depth_target,    mod_depth_current;  /* 0..R_MOD_HEADROOM/2 */
    int   mod_shape;           /* 0=sine, 1=warp, 2=sink */

    /* Input highpass — stops sub-80-Hz energy from piling up in the comb
     * feedback (standard practice for algorithmic reverbs). One-pole. */
    float hp_state_L, hp_state_R;

    /* Stretch (lo-fi) — when on, comb network runs at half sample rate. */
    int   stretch;
    float stretch_last_L, stretch_last_R;
};

/* a = exp(-2π × 80 / 44100) ≈ 0.9887. One-pole HPF: y = x - lowpass(x). */
#define R_HPF_LP_COEF  0.9887f

#define R_SMOOTH_COEF 0.9989f  /* ~20 ms time constant @ 44.1 kHz */

reverb_t* reverb_create(double sample_rate) {
    const double sr = sample_rate > 0.0 ? sample_rate : AMB_REF_SAMPLE_RATE;
    reverb_t *r = (reverb_t*)calloc(1, sizeof(reverb_t));
    if (!r) return NULL;

    /* Scale the comb/allpass geometry to the host rate so the modal density
     * and delay times (in seconds) match the 44.1 kHz design. */
    r->stereo_spread = amb_scale_samples(R_STEREO_SPREAD, sr);
    const int mod_headroom = amb_scale_samples(R_MOD_HEADROOM, sr);

    for (int i = 0; i < R_COMB; i++) {
        r->comb_base[i] = amb_scale_samples(R_COMB_BASE[i], sr);
        int Llen = r->comb_base[i] + mod_headroom;
        int Rlen = r->comb_base[i] + r->stereo_spread + mod_headroom;
        r->comb_buf_len_L[i] = Llen;
        r->comb_buf_len_R[i] = Rlen;
        r->comb_buf_L[i] = (float*)calloc((size_t)Llen, sizeof(float));
        r->comb_buf_R[i] = (float*)calloc((size_t)Rlen, sizeof(float));
        if (!r->comb_buf_L[i] || !r->comb_buf_R[i]) { reverb_destroy(r); return NULL; }
    }
    for (int i = 0; i < R_AP; i++) {
        r->ap_base[i] = amb_scale_samples(R_AP_BASE[i], sr);
        int Llen = r->ap_base[i];
        int Rlen = r->ap_base[i] + r->stereo_spread;
        r->ap_buf_len_L[i] = Llen;
        r->ap_buf_len_R[i] = Rlen;
        r->ap_buf_L[i] = (float*)calloc((size_t)Llen, sizeof(float));
        r->ap_buf_R[i] = (float*)calloc((size_t)Rlen, sizeof(float));
        if (!r->ap_buf_L[i] || !r->ap_buf_R[i]) { reverb_destroy(r); return NULL; }
    }

    /* Time-based coefficients scaled to the host rate. */
    /* Per-comb swing scaled DOWN for the port's reduced comb count. With the
     * plugin's 8 combs the ±45-sample flutter averages into smooth movement;
     * with only R_COMB (3 in the full panel) it stays exposed as audible pitch
     * drift ("falling") under deep Flux — the "broken piano" mode above. Scale
     * by sqrt(R_COMB/8) to keep the flutter-vs-shimmer ratio near the plugin's
     * at any comb count (auto-rises back to 45 if combs are restored). */
    r->mod_depth_max = amb_scale_samples_f(45.0 * sqrtf((float) R_COMB / 8.0f), sr);
    r->hp_a    = amb_scale_onepole(R_HPF_LP_COEF, sr);
    r->smooth_c = amb_scale_onepole(R_SMOOTH_COEF, sr);
    r->fb_target = 0.78f;
    r->fb_current = 0.78f;
    /* Comb-damping is a one-pole LP in each comb's feedback path; rate-scale it
     * so its cutoff (the tail's warmth / lushness) is identical at any sample
     * rate. Exact at 44.1 kHz, so parity holds. Without this, higher rates make
     * the reverb brighter/thinner than the 44.1 kHz Schwung original. */
    r->damp_a = amb_scale_onepole(0.55f, sr);
    r->ap_g = 0.70f;
    r->input_gain = 0.40f;
    r->wet_gain = 0.18f;
    r->base_rate_hz = 0.3f;
    r->mod_depth_target = 0.0f;
    r->mod_depth_current = 0.0f;
    r->mod_shape = 0;
    for (int i = 0; i < R_COMB; i++) {
        lfo_init(&r->lfo[i], (int)sr);
        lfo_set_phase(&r->lfo[i], R_COMB_PHASE[i]);
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
    return r;
}

void reverb_destroy(reverb_t *r) {
    if (!r) return;
    for (int i = 0; i < R_COMB; i++) { free(r->comb_buf_L[i]); free(r->comb_buf_R[i]); }
    for (int i = 0; i < R_AP; i++)   { free(r->ap_buf_L[i]);   free(r->ap_buf_R[i]);   }
    free(r);
}

/* Clear the comb/allpass tails + filter state (RT-safe). Keeps params. */
void reverb_reset(reverb_t *r) {
    if (!r) return;
    for (int i = 0; i < R_COMB; i++) {
        memset(r->comb_buf_L[i], 0, (size_t)r->comb_buf_len_L[i] * sizeof(float));
        memset(r->comb_buf_R[i], 0, (size_t)r->comb_buf_len_R[i] * sizeof(float));
        r->comb_write_L[i] = 0; r->comb_write_R[i] = 0;
    }
    for (int i = 0; i < R_AP; i++) {
        memset(r->ap_buf_L[i], 0, (size_t)r->ap_buf_len_L[i] * sizeof(float));
        memset(r->ap_buf_R[i], 0, (size_t)r->ap_buf_len_R[i] * sizeof(float));
        r->ap_pos_L[i] = 0; r->ap_pos_R[i] = 0;
    }
    r->hp_state_L = r->hp_state_R = 0.0f;
    r->stretch_last_L = r->stretch_last_R = 0.0f;
}

void reverb_set_decay(reverb_t *r, float decay_0_1) {
    if (!r) return;
    if (decay_0_1 < 0.0f) decay_0_1 = 0.0f;
    if (decay_0_1 > 1.0f) decay_0_1 = 1.0f;
    float curve = powf(decay_0_1, 0.4f);
    r->fb_target = 0.50f + 0.49f * curve;
}

void reverb_set_mod_depth(reverb_t *r, float depth_0_1) {
    if (!r) return;
    if (depth_0_1 < 0.0f) depth_0_1 = 0.0f;
    if (depth_0_1 > 1.0f) depth_0_1 = 1.0f;
    /* Knob -> 0..45 samples (~±1 ms) with depth^0.7 curve.
     * Past ~50 samples per comb each delay becomes audibly retuned (the
     * "broken piano" failure mode). 45 samples × 44.1 kHz = ~1 ms ≈ a few
     * cents of detune per comb — perceived as movement, not pitch.
     * Concave curve puts more useful travel in the low/mid knob range. */
    float curve = powf(depth_0_1, 0.7f);
    r->mod_depth_target = curve * r->mod_depth_max;
}

void reverb_set_mod_rate(reverb_t *r, float rate_0_1) {
    if (!r) return;
    if (rate_0_1 < 0.0f) rate_0_1 = 0.0f;
    if (rate_0_1 > 1.0f) rate_0_1 = 1.0f;
    /* Log map: 0 -> 0.05 Hz, 0.5 -> ~0.63 Hz, 1 -> 8 Hz. */
    r->base_rate_hz = 0.05f * expf(rate_0_1 * 5.075f);
    for (int i = 0; i < R_COMB; i++) {
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
}

void reverb_set_mod_rate_hz(reverb_t *r, float hz) {
    if (!r) return;
    if (hz < 0.0f) hz = 0.0f;
    r->base_rate_hz = hz;
    for (int i = 0; i < R_COMB; i++) {
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
}

void reverb_set_mod_shape(reverb_t *r, int shape) {
    if (!r) return;
    if (shape < 0) shape = 0;
    if (shape > 2) shape = 2;
    r->mod_shape = shape;
}

void reverb_set_stretch(reverb_t *r, int on) {
    if (!r) return;
    r->stretch = on ? 1 : 0;
}

static inline float ap_tick(float *buf, int len, int *pos, float g, float x) {
    int p = *pos;
    float s = buf[p];
    float v = x - g * s;
    float y = s + g * v;
    buf[p] = v;
    p++; if (p >= len) p = 0;
    *pos = p;
    return y;
}

static inline float comb_read_interp(const float *buf, int buf_len,
                                     int write_pos, float read_delay) {
    int d_int = (int)read_delay;
    float d_frac = read_delay - (float)d_int;
    int ridx = write_pos - d_int;
    if (ridx < 0) ridx += buf_len;
    int ridx_next = ridx - 1;
    if (ridx_next < 0) ridx_next += buf_len;
    return buf[ridx] * (1.0f - d_frac) + buf[ridx_next] * d_frac;
}

void reverb_process(reverb_t *r,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!r || frames <= 0) return;

    const float damp_a = r->damp_a;
    const float damp_b = 1.0f - damp_a;
    const float ap_g   = r->ap_g;
    const float in_g   = r->input_gain;
    const float out_g  = r->wet_gain;

    /* Smoothed feedback + mod depth — both feed delay buffers, so abrupt
     * changes would echo forever in the reverb tail. */
    float fb_curr    = r->fb_current;
    float mod_curr   = r->mod_depth_current;
    const float fb_t   = r->fb_target;
    const float mod_t  = r->mod_depth_target;
    const float c      = r->smooth_c;
    const float ic     = 1.0f - c;

    const float hp_a   = r->hp_a;
    const float hp_1ma = 1.0f - hp_a;
    float hp_L = r->hp_state_L;
    float hp_R = r->hp_state_R;

    const int stretch = r->stretch;
    float stretch_last_L = r->stretch_last_L;
    float stretch_last_R = r->stretch_last_R;

    for (int n = 0; n < frames; n++) {
        if (stretch && (n & 1)) {
            /* Zero-order-hold previous output; everything below stays frozen
             * (combs, LFO, HPF state). Effective half-rate = doubled delays
             * + bitcrush aliasing artifacts. */
            out_l[n] = stretch_last_L;
            out_r[n] = stretch_last_R;
            continue;
        }
        fb_curr  = c * fb_curr  + ic * fb_t;
        mod_curr = c * mod_curr + ic * mod_t;

        float xL = in_l[n] * in_g;
        float xR = in_r[n] * in_g;
        /* Input HPF — strip sub-~80 Hz before the comb network. */
        hp_L = hp_a * hp_L + hp_1ma * xL;
        hp_R = hp_a * hp_R + hp_1ma * xR;
        xL = xL - hp_L;
        xR = xR - hp_R;

        float sumL = 0.0f, sumR = 0.0f;
        const int shape = r->mod_shape;
        for (int i = 0; i < R_COMB; i++) {
            /* Each comb advances its own LFO at its own rate. */
            float raw_sin = lfo_tick_sine(&r->lfo[i]);
            float mod = lfo_shape_apply(shape, raw_sin) * mod_curr;

            /* L comb */
            float dL = (float)r->comb_base[i] + mod;
            float yL = comb_read_interp(r->comb_buf_L[i],
                                        r->comb_buf_len_L[i],
                                        r->comb_write_L[i], dL);
            float fL = damp_b * yL + damp_a * r->comb_damp_L[i];
            r->comb_damp_L[i] = fL;
            r->comb_buf_L[i][r->comb_write_L[i]] = xL + fb_curr * fL;
            r->comb_write_L[i]++;
            if (r->comb_write_L[i] >= r->comb_buf_len_L[i]) r->comb_write_L[i] = 0;
            sumL += yL;

            /* R comb — same mod offset, longer base delay via stereo spread. */
            float dR = (float)(r->comb_base[i] + r->stereo_spread) + mod;
            float yR = comb_read_interp(r->comb_buf_R[i],
                                        r->comb_buf_len_R[i],
                                        r->comb_write_R[i], dR);
            float fR = damp_b * yR + damp_a * r->comb_damp_R[i];
            r->comb_damp_R[i] = fR;
            r->comb_buf_R[i][r->comb_write_R[i]] = xR + fb_curr * fR;
            r->comb_write_R[i]++;
            if (r->comb_write_R[i] >= r->comb_buf_len_R[i]) r->comb_write_R[i] = 0;
            sumR += yR;
        }

        float oL = sumL, oR = sumR;
        for (int i = 0; i < R_AP; i++) {
            oL = ap_tick(r->ap_buf_L[i], r->ap_buf_len_L[i],
                         &r->ap_pos_L[i], ap_g, oL);
            oR = ap_tick(r->ap_buf_R[i], r->ap_buf_len_R[i],
                         &r->ap_pos_R[i], ap_g, oR);
        }

        out_l[n] = oL * out_g;
        out_r[n] = oR * out_g;
        stretch_last_L = out_l[n];
        stretch_last_R = out_r[n];
    }

    r->fb_current = fb_curr;
    r->mod_depth_current = mod_curr;
    r->hp_state_L = hp_L;
    r->hp_state_R = hp_R;
    r->stretch_last_L = stretch_last_L;
    r->stretch_last_R = stretch_last_R;
}
