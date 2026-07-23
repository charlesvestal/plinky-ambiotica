/* Ambiotica harmony — see harmony.h. */
#include "harmony.h"
#include "fast_math.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define H_MIN_FREQ   28.0f    /* lowest tunable note -> biggest delay buffer */
#ifdef AMB_BUILTIN_REVERB
#define H_IN_GAIN    1.0f     /* native do_reverb wet is ~5x quieter than the modal
                                reverb it replaced; lift the excitation so Spectra
                                still sings the chord out of the (quieter) wash.
                                Only the passthrough term is un-gained, so this
                                does not change the wet level, just the ring. */
#else
#define H_IN_GAIN    0.22f    /* excitation into each high-Q resonator (chord onset) */
#endif
#define H_SMOOTH     0.0008f  /* per-sample glide of delay length (no zipper)  */

struct harmony_s {
    double sr;
    float *buf[HARMONY_MAX_VOICES];   /* mono comb delay line per voice */
    int    buflen;
    int    widx;

    float  delay[HARMONY_MAX_VOICES];        /* current fractional delay (samples) */
    float  delay_target[HARMONY_MAX_VOICES]; /* requested delay                    */
    float  lp[HARMONY_MAX_VOICES];           /* damping one-pole state             */
    float  gL[HARMONY_MAX_VOICES], gR[HARMONY_MAX_VOICES];  /* per-voice pan        */
    int    n_voices;
    float  damp_a;                  /* damping cutoff coef */
    float  amount, amount_sm, amount_a;
    float  fb;                      /* resonator feedback (ring length) */
    float  out_gain;
};

harmony_t* harmony_create(double sample_rate) {
    harmony_t *h = (harmony_t*) calloc(1, sizeof(harmony_t));
    if (!h) return NULL;
    h->sr = sample_rate > 0.0 ? sample_rate : 44100.0;
    h->buflen = (int)(h->sr / H_MIN_FREQ) + 4;
    for (int v = 0; v < HARMONY_MAX_VOICES; v++) {
        h->buf[v] = (float*) calloc((size_t) h->buflen, sizeof(float));
        if (!h->buf[v]) { harmony_destroy(h); return NULL; }
        h->delay[v] = h->delay_target[v] = (float)(h->sr / 220.0);  /* A3 default */
        /* spread voices L..R */
        float pan = (HARMONY_MAX_VOICES > 1) ? (float) v / (float)(HARMONY_MAX_VOICES - 1) : 0.5f;
        float th = (0.5f + (pan - 0.5f) * 0.8f) * 1.5707963f;       /* keep both chans */
        h->gL[v] = fast_cosf(th); h->gR[v] = fast_sinf(th);
    }
    h->n_voices = 0;
    /* damping ~8 kHz one-pole — bright, long ring (still tames the very top) */
    h->damp_a   = 1.0f - expf(-6.2831853f * 8000.0f / (float) h->sr);
    h->amount_a = 1.0f - expf(-1.0f / (0.020f * (float) h->sr));     /* ~20 ms */
    h->fb       = 0.995f;           /* default ring length (see harmony_set_ring) */
    h->out_gain = 1.25f;            /* chord level in the wash (louder, ring stays finite) */
    return h;
}

void harmony_destroy(harmony_t *h) {
    if (!h) return;
    for (int v = 0; v < HARMONY_MAX_VOICES; v++) free(h->buf[v]);
    free(h);
}

void harmony_reset(harmony_t *h) {
    if (!h) return;
    for (int v = 0; v < HARMONY_MAX_VOICES; v++) {
        memset(h->buf[v], 0, (size_t) h->buflen * sizeof(float));
        h->lp[v] = 0.0f;
    }
    h->widx = 0; h->amount_sm = 0.0f;
}

void harmony_set_chord(harmony_t *h, const float *freqs, int n) {
    if (!h || !freqs) return;
    if (n < 0) n = 0; if (n > HARMONY_MAX_VOICES) n = HARMONY_MAX_VOICES;
    const int prev = h->n_voices;      /* voices already ringing at a pitch */
    h->n_voices = n;
    for (int v = 0; v < n; v++) {
        float f = freqs[v];
        if (f < H_MIN_FREQ) f = H_MIN_FREQ;
        float d = (float)(h->sr / (double) f);
        if (d > (float)(h->buflen - 2)) d = (float)(h->buflen - 2);
        if (d < 2.0f) d = 2.0f;
        h->delay_target[v] = d;
        /* Newly-voiced note: SNAP the delay to pitch instead of gliding from the
         * A3 create-time default — that glide was an audible downward swoop each
         * time the chord first engaged. Voices already ringing still glide (so a
         * future key change bends smoothly rather than clicks). */
        if (v >= prev) h->delay[v] = d;
    }
}

/* Ring length: 0 = shorter/plucky, 1 = long (but still finite) sustained chord.
 * Capped below self-sustain so the chord always dies out when input stops. */
void harmony_set_ring(harmony_t *h, float ring_0_1) {
    if (!h) return;
    if (ring_0_1 < 0.0f) ring_0_1 = 0.0f;
    if (ring_0_1 > 1.0f) ring_0_1 = 1.0f;
    h->fb = 0.982f + 0.011f * ring_0_1;   /* 0.982 .. 0.993 (decays in finite time) */
}

void harmony_set_amount(harmony_t *h, float amount) {
    if (!h) return;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    h->amount = amount;
}

static inline float h_read(const float *buf, int len, int widx, float delay) {
    float rp = (float) widx - delay;
    while (rp < 0.0f) rp += (float) len;
    int i0 = (int) rp; float fr = rp - (float) i0;
    int i1 = i0 + 1; if (i1 >= len) i1 = 0;
    return buf[i0] + (buf[i1] - buf[i0]) * fr;
}

void harmony_process(harmony_t *h,
                     const float *in_l, const float *in_r,
                     float *out_l, float *out_r,
                     int frames) {
    if (!h || frames <= 0) return;

    /* Bypass once fully ramped down and inactive (parity-safe default). */
    if (h->amount <= 0.0f && h->amount_sm <= 1.0e-5f) {
        h->amount_sm = 0.0f;
        for (int n = 0; n < frames; n++) { out_l[n] = in_l[n]; out_r[n] = in_r[n]; }
        return;
    }

    const int nv = h->n_voices;
    const int len = h->buflen;
    int widx = h->widx;
    float a = h->amount_sm;
    const float aa = h->amount_a, at = h->amount;
    const float fb = h->fb, ig = H_IN_GAIN, da = h->damp_a, og = h->out_gain;

    for (int n = 0; n < frames; n++) {
        a += aa * (at - a);
        const float inMono = 0.5f * (in_l[n] + in_r[n]);
        float oL = 0.0f, oR = 0.0f;
        for (int v = 0; v < nv; v++) {
            h->delay[v] += H_SMOOTH * (h->delay_target[v] - h->delay[v]);   /* glide */
            float r = h_read(h->buf[v], len, widx, h->delay[v]);
            h->lp[v] += da * (r - h->lp[v]);                /* damp the feedback */
            /* High-Q resonator WITH gain at resonance (sings the pitch out of
             * the broadband wash); tanh keeps the ring from howling. */
            h->buf[v][widx] = ig * inMono + fast_tanhf(fb * h->lp[v]);
            oL += r * h->gL[v];
            oR += r * h->gR[v];
        }
        /* ADD the resonated chord on top of the wash (don't replace it), so
         * turning Harmony up layers the chord without killing the reverb. */
        out_l[n] = in_l[n] + a * oL * og;
        out_r[n] = in_r[n] + a * oR * og;
        widx++; if (widx >= len) widx = 0;
    }

    h->widx = widx;
    h->amount_sm = a;
}
