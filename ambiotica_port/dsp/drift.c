/* Ambiotica drift — see drift.h. */
#include "drift.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DRIFT_TWO_PI      6.28318530717958647692f
/* Very slow, slightly-different L/R rates so the wander is wide and organic. */
#define DRIFT_RATE_L      0.070f    /* Hz */
#define DRIFT_RATE_R      0.083f    /* Hz */
#define DRIFT_DEPTH_MAX   380.0f    /* peak delay swing (samples) at amount=1   */
#define DRIFT_CENTER_PAD  24.0f     /* keep the read head off the write head    */

struct drift_s {
    double sr;
    float  amount;

    float *buf_l, *buf_r;
    int    buf_len;
    int    widx;

    float  phase_l, phase_r;       /* 0..1 LFO phases            */
    float  inc_l,   inc_r;         /* per-sample phase increment */
    float  center;                 /* max nominal delay (samples) */
    float  amount_target;          /* knob target (0..1)         */
    float  amount_sm;              /* per-sample smoothed amount  */
    float  amount_a;               /* smoothing coef (~30 ms)    */
};

drift_t* drift_create(double sample_rate) {
    drift_t *d = (drift_t*) calloc(1, sizeof(drift_t));
    if (!d) return NULL;
    d->sr = sample_rate > 0.0 ? sample_rate : 44100.0;

    d->center  = DRIFT_DEPTH_MAX + DRIFT_CENTER_PAD;
    d->buf_len = (int) (2.0f * d->center + 4.0f);

    d->buf_l = (float*) calloc((size_t) d->buf_len, sizeof(float));
    d->buf_r = (float*) calloc((size_t) d->buf_len, sizeof(float));
    if (!d->buf_l || !d->buf_r) { drift_destroy(d); return NULL; }

    d->inc_l = (float) (DRIFT_RATE_L / d->sr);
    d->inc_r = (float) (DRIFT_RATE_R / d->sr);
    d->phase_r = 0.25f;            /* start in quadrature */
    d->amount_a = 1.0f - (float) exp(-1.0 / (0.030 * d->sr));   /* ~30 ms */
    d->amount_sm = 0.0f;
    drift_set_amount(d, 0.0f);
    return d;
}

void drift_destroy(drift_t *d) {
    if (!d) return;
    free(d->buf_l); free(d->buf_r); free(d);
}

/* Clear the delay buffer + smoothing state (RT-safe). Keeps the amount target. */
void drift_reset(drift_t *d) {
    if (!d) return;
    memset(d->buf_l, 0, (size_t)d->buf_len * sizeof(float));
    memset(d->buf_r, 0, (size_t)d->buf_len * sizeof(float));
    d->widx = 0;
    d->amount_sm = 0.0f;
}

void drift_set_amount(drift_t *d, float amount) {
    if (!d) return;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    d->amount = amount;
    d->amount_target = amount;     /* process() ramps amount_sm toward this */
}

/* Fractional read from a delay line, `delay` samples behind the write head. */
static inline float read_frac(const float *buf, int len, int widx, float delay) {
    float rp = (float) widx - delay;
    while (rp < 0.0f)        rp += (float) len;
    while (rp >= (float) len) rp -= (float) len;
    int i0 = (int) rp;
    int i1 = i0 + 1; if (i1 >= len) i1 = 0;
    float f = rp - (float) i0;
    return buf[i0] + (buf[i1] - buf[i0]) * f;
}

void drift_process(drift_t *d,
                  const float *in_l, const float *in_r,
                  float *out_l, float *out_r,
                  int frames) {
    if (!d || frames <= 0) return;

    /* Bypass only once fully ramped down (parity-safe default). */
    if (d->amount_target <= 0.0f && d->amount_sm <= 1.0e-5f) {
        d->amount_sm = 0.0f;
        for (int n = 0; n < frames; n++) { out_l[n] = in_l[n]; out_r[n] = in_r[n]; }
        return;
    }

    int widx = d->widx, len = d->buf_len;
    float pl = d->phase_l, pr = d->phase_r;
    const float il = d->inc_l, ir = d->inc_r;
    const float center = d->center;
    float a = d->amount_sm;
    const float aa = d->amount_a, at = d->amount_target;

    for (int n = 0; n < frames; n++) {
        d->buf_l[widx] = in_l[n];
        d->buf_r[widx] = in_r[n];

        a += aa * (at - a);                 /* smooth knob changes (no zipper) */

        /* Nominal delay and modulation depth both scale with the smoothed
         * amount, so the read head grows out from 0 as Drift comes up — no
         * bypass click and no comb at low settings. */
        float ml = sinf(DRIFT_TWO_PI * pl);
        float mr = sinf(DRIFT_TWO_PI * pr);
        float dlyL = center * a + DRIFT_DEPTH_MAX * a * ml;
        float dlyR = center * a + DRIFT_DEPTH_MAX * a * mr;
        if (dlyL < 0.0f) dlyL = 0.0f;
        if (dlyR < 0.0f) dlyR = 0.0f;

        out_l[n] = read_frac(d->buf_l, len, widx, dlyL);
        out_r[n] = read_frac(d->buf_r, len, widx, dlyR);

        pl += il; if (pl >= 1.0f) pl -= 1.0f;
        pr += ir; if (pr >= 1.0f) pr -= 1.0f;
        widx++;   if (widx >= len) widx = 0;
    }

    d->widx = widx; d->phase_l = pl; d->phase_r = pr; d->amount_sm = a;
}
