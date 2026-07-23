/* Dattorro plate reverb — see dattorro.h. */
#include "dattorro.h"
#include "fast_math.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Reference delay lengths from Dattorro's paper, at his 29761 Hz clock. Scaled to
 * the plate's internal (half host) rate in dattorro_create(). */
#define DREF_SR       29761.0f
static const int IN_AP[4]   = { 142, 107, 379, 277 };   /* input diffusers */
static const int MODAP[2]   = { 672, 908 };             /* decay diffusion 1 (modulated) */
static const int DELAYA[2]  = { 4453, 4217 };
static const int AP2[2]     = { 1800, 2656 };           /* decay diffusion 2 */
static const int DELAYB[2]  = { 3720, 3163 };
/* Output taps: {line, offset} pairs. line: 0..1 delayA L/R, 2..3 ap2 L/R, 4..5 delayB L/R. */
static const int TAP_L[7][2] = { {1,266},{1,2974},{3,1913},{5,1996},{0,1990},{2,187},{4,1066} };
static const int TAP_L_SGN[7] = { +1,+1,-1,+1,-1,-1,-1 };
static const int TAP_R[7][2] = { {0,353},{0,3627},{2,1228},{4,2673},{1,2111},{3,335},{5,121} };
static const int TAP_R_SGN[7] = { +1,+1,-1,+1,-1,-1,-1 };

/* ------- delay line (ring buffer): read `offset` samples ago, then write+advance -- */
typedef struct { float* buf; int len; int pos; } dline;

struct dattorro_s {
    double sr;
    float  scale;                 /* DREF_SR-samples -> internal-rate samples */
    int    inap_len[4], modap_len[2], da_len[2], ap2_len[2], db_len[2];
    dline  inap[4];               /* input diffusers */
    dline  modap[2], da[2], ap2[2], db[2];   /* tank: L=0, R=1 */
    int    tapL[7], tapR[7];      /* scaled tap offsets */
    float  bw_lp;                 /* input bandwidth one-pole */
    float  damp_lp[2];            /* tank damping one-pole (L,R) */
    float  lfo_ph[2];             /* two slow modulation LFOs */
    float  lfo_inc;
    /* params */
    float  decay, dd1, dd2, damp_a, bw, mod_depth;
    /* half-rate upsample hold */
    float  holdL, holdR;
};

static int dl_alloc(dline* d, int len) {
    if (len < 2) len = 2;
    d->buf = (float*)calloc((size_t)len, sizeof(float));
    d->len = len; d->pos = 0;
    return d->buf != NULL;
}
static inline float dl_read(const dline* d, int offset) {
    int i = d->pos - offset; if (i < 0) i += d->len;
    return d->buf[i];
}
static inline float dl_read_frac(const dline* d, float offset) {
    int i0 = (int)offset; float fr = offset - (float)i0;
    int a = d->pos - i0;     if (a < 0) a += d->len;
    int b = a - 1;           if (b < 0) b += d->len;
    return d->buf[a] + (d->buf[b] - d->buf[a]) * fr;
}
static inline void dl_write(dline* d, float x) {
    d->buf[d->pos] = x; d->pos++; if (d->pos >= d->len) d->pos = 0;
}
/* Schroeder allpass over a delay line (stores the recirculating v). */
static inline float ap_tick(dline* d, int D, float in, float g) {
    float vd = dl_read(d, D);
    float v  = in + g * vd;
    dl_write(d, v);
    return -g * v + vd;
}
static inline float modap_tick(dline* d, int D, float in, float g, float modsamp) {
    float vd = dl_read_frac(d, (float)D + modsamp);
    float v  = in + g * vd;
    dl_write(d, v);
    return -g * v + vd;
}
/* pure delay: output = input D samples ago. */
static inline float delay_tick(dline* d, int D, float in) {
    float o = dl_read(d, D);
    dl_write(d, in);
    return o;
}

static int scl(dattorro_t* d, int ref) { int v = (int)(ref * d->scale + 0.5f); return v < 1 ? 1 : v; }

dattorro_t* dattorro_create(double sample_rate) {
    dattorro_t* d = (dattorro_t*)calloc(1, sizeof(dattorro_t));
    if (!d) return NULL;
    d->sr    = sample_rate > 0 ? sample_rate : 32000.0;
    float internal = (float)(d->sr * 0.5);          /* plate runs at half host rate */
    d->scale = internal / DREF_SR;
    /* modulation: ~1.0 Hz LFOs at the internal rate */
    d->lfo_inc = 6.2831853f * 1.0f / internal;
    d->lfo_ph[0] = 0.0f; d->lfo_ph[1] = 2.3f;
    /* delay lengths */
    int ok = 1;
    for (int i = 0; i < 4; i++) { d->inap_len[i] = scl(d, IN_AP[i]); ok &= dl_alloc(&d->inap[i], d->inap_len[i] + 2); }
    const int modroom = (int)(0.020f * internal) + 4;   /* ~20 ms mod headroom */
    for (int c = 0; c < 2; c++) {
        d->modap_len[c] = scl(d, MODAP[c]);   ok &= dl_alloc(&d->modap[c], d->modap_len[c] + modroom);
        d->da_len[c]    = scl(d, DELAYA[c]);  ok &= dl_alloc(&d->da[c],    d->da_len[c]  + 2);
        d->ap2_len[c]   = scl(d, AP2[c]);     ok &= dl_alloc(&d->ap2[c],   d->ap2_len[c] + 2);
        d->db_len[c]    = scl(d, DELAYB[c]);  ok &= dl_alloc(&d->db[c],    d->db_len[c]  + 2);
    }
    for (int i = 0; i < 7; i++) { d->tapL[i] = scl(d, TAP_L[i][1]); d->tapR[i] = scl(d, TAP_R[i][1]); }
    if (!ok) { dattorro_destroy(d); return NULL; }
    d->dd1 = 0.70f; d->bw = 0.9995f; d->mod_depth = 0.5f;
    dattorro_set_decay(d, 0.5f);
    dattorro_set_damp(d, 0.35f);
    return d;
}

void dattorro_destroy(dattorro_t* d) {
    if (!d) return;
    for (int i = 0; i < 4; i++) free(d->inap[i].buf);
    for (int c = 0; c < 2; c++) { free(d->modap[c].buf); free(d->da[c].buf); free(d->ap2[c].buf); free(d->db[c].buf); }
    free(d);
}

void dattorro_reset(dattorro_t* d) {
    if (!d) return;
    for (int i = 0; i < 4; i++) memset(d->inap[i].buf, 0, (size_t)d->inap[i].len * sizeof(float));
    for (int c = 0; c < 2; c++) {
        memset(d->modap[c].buf, 0, (size_t)d->modap[c].len * sizeof(float));
        memset(d->da[c].buf,    0, (size_t)d->da[c].len    * sizeof(float));
        memset(d->ap2[c].buf,   0, (size_t)d->ap2[c].len   * sizeof(float));
        memset(d->db[c].buf,    0, (size_t)d->db[c].len    * sizeof(float));
    }
    d->bw_lp = d->damp_lp[0] = d->damp_lp[1] = 0.0f;
    d->holdL = d->holdR = 0.0f;
}

void dattorro_set_decay(dattorro_t* d, float x) {
    if (!d) return; if (x < 0) x = 0; if (x > 1) x = 1;
    /* map to internal decay 0.40 .. 0.95 -> RT60 ~1.1..~12 s, tracking the plugin
     * reverb (measured 1.34..12.45 s) and capped short of self-oscillation. */
    d->decay = 0.40f + 0.55f * x;
    d->dd2   = 0.15f + d->decay; if (d->dd2 > 0.50f) d->dd2 = 0.50f; if (d->dd2 < 0.25f) d->dd2 = 0.25f;
}
void dattorro_set_damp(dattorro_t* d, float x) {
    if (!d) return; if (x < 0) x = 0; if (x > 1) x = 1;
    /* damp_a = per-sample one-pole retention of the feedback low-pass. 1 = bright. */
    d->damp_a = 1.0f - (0.05f + 0.85f * x);   /* 0.95 (bright) .. 0.10 (dark) */
}
void dattorro_set_mod(dattorro_t* d, float x) {
    if (!d) return; if (x < 0) x = 0; if (x > 1) x = 1;
    d->mod_depth = x;
}

/* One half-rate stereo tick. */
static inline void dattorro_tick(dattorro_t* d, float inL, float inR, float* yl, float* yr) {
    float mono = 0.5f * (inL + inR);
    d->bw_lp += d->bw * (mono - d->bw_lp);
    float x = d->bw_lp;
    x = ap_tick(&d->inap[0], d->inap_len[0], x, 0.75f);
    x = ap_tick(&d->inap[1], d->inap_len[1], x, 0.75f);
    x = ap_tick(&d->inap[2], d->inap_len[2], x, 0.625f);
    x = ap_tick(&d->inap[3], d->inap_len[3], x, 0.625f);

    d->lfo_ph[0] += d->lfo_inc; if (d->lfo_ph[0] > 6.2831853f) d->lfo_ph[0] -= 6.2831853f;
    d->lfo_ph[1] += d->lfo_inc; if (d->lfo_ph[1] > 6.2831853f) d->lfo_ph[1] -= 6.2831853f;
    float modA = (0.5f + 0.5f * fast_sinf(d->lfo_ph[0])) * d->mod_depth * 12.0f;   /* 0..~12 samples */
    float modB = (0.5f + 0.5f * fast_sinf(d->lfo_ph[1])) * d->mod_depth * 12.0f;

    float fbL = dl_read(&d->db[0], d->db_len[0]);   /* delayed outputs of each half */
    float fbR = dl_read(&d->db[1], d->db_len[1]);
    const float dec = d->decay;

    /* LEFT half: fed by the RIGHT half's tail */
    float a = x + dec * fbR;
    a = modap_tick(&d->modap[0], d->modap_len[0], a, d->dd1, modA);
    a = delay_tick(&d->da[0], d->da_len[0], a);
    d->damp_lp[0] += d->damp_a * (a - d->damp_lp[0]); a = d->damp_lp[0];
    a *= dec;
    a = ap_tick(&d->ap2[0], d->ap2_len[0], a, d->dd2);
    dl_write(&d->db[0], a);

    /* RIGHT half: fed by the LEFT half's tail */
    float b = x + dec * fbL;
    b = modap_tick(&d->modap[1], d->modap_len[1], b, d->dd1, modB);
    b = delay_tick(&d->da[1], d->da_len[1], b);
    d->damp_lp[1] += d->damp_a * (b - d->damp_lp[1]); b = d->damp_lp[1];
    b *= dec;
    b = ap_tick(&d->ap2[1], d->ap2_len[1], b, d->dd2);
    dl_write(&d->db[1], b);

    /* multi-tap stereo output */
    const dline* line[6] = { &d->da[0], &d->da[1], &d->ap2[0], &d->ap2[1], &d->db[0], &d->db[1] };
    float ol = 0.f, orr = 0.f;
    for (int i = 0; i < 7; i++) {
        ol  += TAP_L_SGN[i] * dl_read(line[TAP_L[i][0]], d->tapL[i]);
        orr += TAP_R_SGN[i] * dl_read(line[TAP_R[i][0]], d->tapR[i]);
    }
    *yl = ol * 0.6f; *yr = orr * 0.6f;   /* 0.6: tap sum headroom */
}

void dattorro_process(dattorro_t* d, const float* in_l, const float* in_r,
                      float* out_l, float* out_r, int n) {
    if (!d) { for (int i = 0; i < n; i++) { out_l[i] = out_r[i] = 0.f; } return; }
    for (int i = 0; i < n; i += 2) {
        float il = 0.5f * (in_l[i] + (i + 1 < n ? in_l[i + 1] : in_l[i]));   /* 32k -> 16k decimate */
        float ir = 0.5f * (in_r[i] + (i + 1 < n ? in_r[i + 1] : in_r[i]));
        float wl, wr;
        dattorro_tick(d, il, ir, &wl, &wr);
        out_l[i] = 0.5f * (d->holdL + wl);          /* 16k -> 32k linear upsample */
        out_r[i] = 0.5f * (d->holdR + wr);
        if (i + 1 < n) { out_l[i + 1] = wl; out_r[i + 1] = wr; }
        d->holdL = wl; d->holdR = wr;
    }
}
