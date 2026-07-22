/* Ambiotica bloom — see bloom.h. */
#include "bloom.h"
#include <stdlib.h>
#include <math.h>

/* Follower time constants (seconds). The fast follower tracks the true
 * envelope; the slow follower's ATTACK is what the knob stretches. Both use a
 * fast release so that when a sound decays the ratio returns to 1 and tails are
 * never ducked. */
#define BLOOM_FAST_T      0.0020f   /* 2 ms — fast envelope attack & release */
#define BLOOM_SLOW_REL_T  0.0050f   /* 5 ms — slow follower release          */
#define BLOOM_SLOW_ATK_MIN 0.0020f  /* amount=0 → equals fast → bypass        */
#define BLOOM_SLOW_ATK_MAX 2.0000f  /* amount=1 → ~2 s bowed swell            */
#define BLOOM_GAIN_T      0.0050f   /* 5 ms smoothing on the applied gain     */
#define BLOOM_EPS         1.0e-6f

struct bloom_s {
    double sr;
    float  amount;

    float  env_fast_l, env_fast_r;   /* fast |x| follower            */
    float  env_slow_l, env_slow_r;   /* slow-attack |x| follower     */
    float  gain_l, gain_r;           /* smoothed applied gain        */

    float  fast_a;                   /* fast attack/release coef     */
    float  slow_atk_a;               /* slow follower attack coef    */
    float  slow_rel_a;               /* slow follower release coef   */
    float  gain_a;                   /* gain smoothing coef          */
};

/* One-pole coefficient for a time constant (a = exp(-1/(t*sr))). */
static float coef_for(float t_seconds, double sr) {
    if (t_seconds <= 0.0f) return 0.0f;
    return (float) exp(-1.0 / ((double) t_seconds * sr));
}

bloom_t* bloom_create(double sample_rate) {
    bloom_t *b = (bloom_t*) calloc(1, sizeof(bloom_t));
    if (!b) return NULL;
    b->sr = sample_rate > 0.0 ? sample_rate : 44100.0;
    b->fast_a    = coef_for(BLOOM_FAST_T, b->sr);
    b->slow_rel_a = coef_for(BLOOM_SLOW_REL_T, b->sr);
    b->gain_a    = coef_for(BLOOM_GAIN_T, b->sr);
    b->gain_l = b->gain_r = 1.0f;
    bloom_set_amount(b, 0.0f);
    return b;
}

void bloom_destroy(bloom_t *b) { free(b); }

/* Clear the follower/gain state (RT-safe). Keeps the amount setting. */
void bloom_reset(bloom_t *b) {
    if (!b) return;
    b->env_fast_l = b->env_fast_r = 0.0f;
    b->env_slow_l = b->env_slow_r = 0.0f;
    b->gain_l = b->gain_r = 1.0f;
}

void bloom_set_amount(bloom_t *b, float amount) {
    if (!b) return;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    b->amount = amount;
    /* Curve the swell time so the knob feels musical (more travel up high). */
    float t = BLOOM_SLOW_ATK_MIN +
              (BLOOM_SLOW_ATK_MAX - BLOOM_SLOW_ATK_MIN) * (amount * amount);
    b->slow_atk_a = coef_for(t, b->sr);
}

/* Asymmetric one-pole follower of a rectified sample. */
static inline float follow(float env, float x, float a_atk, float a_rel) {
    float a = (x > env) ? a_atk : a_rel;
    return a * env + (1.0f - a) * x;
}

void bloom_process(bloom_t *b,
                   const float *in_l, const float *in_r,
                   float *out_l, float *out_r,
                   int frames) {
    if (!b || frames <= 0) return;

    /* True bypass at amount 0 — identical to input (parity-safe default). */
    if (b->amount <= 0.0f) {
        for (int n = 0; n < frames; n++) { out_l[n] = in_l[n]; out_r[n] = in_r[n]; }
        return;
    }

    float ef_l = b->env_fast_l, ef_r = b->env_fast_r;
    float es_l = b->env_slow_l, es_r = b->env_slow_r;
    float g_l  = b->gain_l,     g_r  = b->gain_r;
    const float fa = b->fast_a, sa = b->slow_atk_a, sr_ = b->slow_rel_a, ga = b->gain_a;

    for (int n = 0; n < frames; n++) {
        float xl = in_l[n], xr = in_r[n];
        float al = fabsf(xl), ar = fabsf(xr);

        ef_l = follow(ef_l, al, fa, fa);
        ef_r = follow(ef_r, ar, fa, fa);
        es_l = follow(es_l, al, sa, sr_);
        es_r = follow(es_r, ar, sa, sr_);

        /* Gain = slow/fast, clamped to 1: <1 during onsets, →1 once the swell
         * completes and during decays. */
        float tg_l = es_l / (ef_l + BLOOM_EPS);
        float tg_r = es_r / (ef_r + BLOOM_EPS);
        if (tg_l > 1.0f) tg_l = 1.0f;
        if (tg_r > 1.0f) tg_r = 1.0f;

        g_l = ga * g_l + (1.0f - ga) * tg_l;
        g_r = ga * g_r + (1.0f - ga) * tg_r;

        out_l[n] = xl * g_l;
        out_r[n] = xr * g_r;
    }

    b->env_fast_l = ef_l; b->env_fast_r = ef_r;
    b->env_slow_l = es_l; b->env_slow_r = es_r;
    b->gain_l = g_l;      b->gain_r = g_r;
}
