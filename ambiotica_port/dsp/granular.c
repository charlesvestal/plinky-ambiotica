/* Granular stage — 8-grain scheduler over a 2s capture ring. */
#include "granular.h"
#include "fast_math.h"
#include "lfo.h"
#include "rate_util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

#define G_SAMPLE_RATE       44100
/* 5 s capture ring. Must exceed 2*grain_max + max_scatter so a REVERSED grain
 * (reads backward `len` while write advances `len` = 2*len of travel, plus the
 * scatter offset) cannot lap the ring and meet the write head mid-grain — that
 * crossing reads a full-buffer content jump at high envelope gain (an audible
 * click). 2*1.5s + 1s = 4s, so 5s leaves margin. (Was 2 s, which lapped.) */
#define G_BUF_SAMPLES       (5 * G_SAMPLE_RATE)       /* 5 s stereo capture */
#define G_MAX_SCATTER_SAMP  G_SAMPLE_RATE             /* 1 s — fixed (was buf/2, =1s @2s buf) */
#define G_MAX_GRAINS        12                    /* headroom for denser overlap */
#define G_GRAIN_MIN_SAMPLES 4410                  /* 100 ms — long, smooth (was 10 ms) */
#define G_GRAIN_MAX_SAMPLES 66150                 /* 1.5 s — stretched pad (was 500 ms) */

/* Pitch quantization set — universally consonant intervals.
 * Octaves and perfect fifths are tonally neutral (work in major, minor,
 * modal, drone). Avoiding thirds/sixths keeps Ambiotica from accidentally
 * imposing a key on the source material. */
static const float G_PITCH_STEPS[5] = {
    1.0f,                  /* unison */
    2.0f,                  /* +1 octave */
    0.5f,                  /* −1 octave */
    1.4983070768766815f,   /* +P5  (2^(+7/12)) */
    0.6674199270738774f    /* −P5  (2^(−7/12)) */
};

typedef struct {
    int   active;
    float read_pos;       /* fractional position in buffer (L & R share) */
    float read_step;      /* per-sample increment = pitch ratio (negative = reversed) */
    int   age;            /* samples since spawn */
    int   length;         /* total samples in this grain */
    int   reversed;       /* Dilate: 1 = backward read + reverse-swell envelope */
} grain_t;

struct granular_s {
    float *buf_L;
    float *buf_R;
    int    buf_len;
    int    write_pos;

    grain_t grains[G_MAX_GRAINS];
    int     samples_to_next;     /* countdown to next grain spawn */
    unsigned spawn_count;        /* monotonic # of grains spawned (GUI tap, read-only) */

    /* Rate-scaled grain-length bounds (equal G_GRAIN_*_SAMPLES at 44.1 kHz). */
    int     grain_min;
    int     grain_max;
    int     max_scatter;   /* rate-scaled (1 s @44.1k) — decoupled from buf_len */

    /* Params */
    float grain_size_0_1;
    float scatter_0_1;
    float reverse_0_1;       /* "Dilate": fraction of grains that play backward */

    /* LFO modulation — wobbles each active grain's pitch in sync with
     * reverb's mod, giving coherent breathing across both stages. */
    lfo_t mod_lfo;
    float mod_depth_cents;   /* 0..100 cents (1 semitone) at full depth */

    /* LCG RNG */
    uint32_t rng;
};

static inline uint32_t lcg_next(uint32_t *s) {
    *s = (*s) * 1103515245u + 12345u;
    return *s;
}
/* Uniform float in [-1, +1]. */
static inline float lcg_bipolar(uint32_t *s) {
    uint32_t v = lcg_next(s);
    return (float)v * (2.0f / 4294967295.0f) - 1.0f;
}
/* Uniform float in [0, 1). */
static inline float lcg_unipolar(uint32_t *s) {
    uint32_t v = lcg_next(s);
    return (float)v * (1.0f / 4294967295.0f);
}

static int current_grain_length(const granular_t *g) {
    int span = g->grain_max - g->grain_min;
    return g->grain_min + (int)((float)span * g->grain_size_0_1);
}

static void spawn_grain(granular_t *g) {
    /* Find a free slot — drop the spawn if none free. */
    grain_t *gr = NULL;
    for (int i = 0; i < G_MAX_GRAINS; i++) {
        if (!g->grains[i].active) { gr = &g->grains[i]; break; }
    }
    if (!gr) return;

    int len = current_grain_length(g);
    gr->length = len;
    gr->age = 0;

    /* Pitch — quantize to unison / ±octave / ±fifth (G_PITCH_STEPS).
     * Scatter controls probability of leaving unison; when it does, the
     * non-unison interval is picked uniformly from the four others.
     *   scatter=0   → 100% unison
     *   scatter=0.5 → 60% unison, 10% each non-unison
     *   scatter=1   → 20% each (all 5 intervals equally likely) */
    float pick = lcg_unipolar(&g->rng);
    float p_unity = 1.0f - g->scatter_0_1 * 0.8f;
    int   idx;
    if (pick < p_unity) {
        idx = 0;  /* unison */
    } else {
        float pick2 = lcg_unipolar(&g->rng);
        int j = (int)(pick2 * 4.0f);
        if (j >= 4) j = 3;
        idx = j + 1;  /* 1..4 → ±oct, ±P5 */
    }
    gr->read_step = G_PITCH_STEPS[idx];

    /* Start position — read from `len` samples behind write_pos so the
     * grain stays in valid past audio for its full duration (even at +1 oct
     * pitch the read just catches up to write at grain end). Scatter then
     * offsets the start by up to max_scatter (1 s) FURTHER INTO THE PAST.
     *
     * The offset must be into the past only (|scatter_off|): a negative offset
     * would place the read window ahead of / less than `len` behind the write
     * head, so the forward read crosses the write seam mid-grain and reads a
     * full-buffer content jump at high envelope gain (an audible click). This is
     * the same guard the reversed path applies below (see rev_scatter). */
    int latency = len;
    int max_scatter = g->max_scatter;
    int scatter_off = (int)((float)max_scatter * g->scatter_0_1 * lcg_bipolar(&g->rng));
    if (scatter_off < 0) scatter_off = -scatter_off;
    int start = g->write_pos - latency - scatter_off;
    /* Bring into [0, buf_len). */
    while (start < 0)            start += g->buf_len;
    while (start >= g->buf_len)  start -= g->buf_len;

    /* Dilate: this grain plays BACKWARD. Forced to UNISON (read_step -1) so the
     * reversed read stays in tune — the ±oct/±5th pitch scatter on a backward read
     * is what sounds out of key. Reads the last `len` samples from their end back
     * to the start (a clean, valid past-audio window). Probability = reverse_0_1,
     * so a bool toggle (0/1) flips the whole cloud. The reverse-swell envelope is
     * applied in process() (see gr->reversed). */
    if (g->reverse_0_1 > 0.0f && lcg_unipolar(&g->rng) < g->reverse_0_1) {
        gr->reversed = 1;
        gr->read_step = -1.0f;                          // unison, backward
        /* The backward read begins at the window END and moves AWAY from the
         * advancing write head — but ONLY if the window sits behind write. A
         * negative scatter would put the end ahead of write, so the backward read
         * would cross the write head mid-grain (reading a buffer-sized content jump
         * at high envelope gain → a click). Force the scatter into the past so the
         * whole reversed window stays behind write. With the 5 s ring (see
         * G_BUF_SAMPLES) this guarantees 2*len + scatter < buf_len, so the read
         * also can't lap the ring back into the write head. */
        int rev_scatter = scatter_off < 0 ? -scatter_off : scatter_off;
        int rstart = g->write_pos - latency - rev_scatter;
        while (rstart < 0)           rstart += g->buf_len;
        while (rstart >= g->buf_len) rstart -= g->buf_len;
        float endp = (float)rstart + (float)len;        // = write_pos - rev_scatter <= write_pos
        while (endp >= (float)g->buf_len) endp -= (float)g->buf_len;
        gr->read_pos = endp;
    } else {
        gr->reversed = 0;
        gr->read_pos = (float)start;
    }

    gr->active = 1;
    g->spawn_count++;            /* GUI tap: one real grain onset */
}

unsigned granular_grain_spawns(const granular_t *g) {
    return g ? g->spawn_count : 0u;
}

granular_t* granular_create(double sample_rate) {
    const double sr = sample_rate > 0.0 ? sample_rate : AMB_REF_SAMPLE_RATE;
    granular_t *g = (granular_t*)calloc(1, sizeof(granular_t));
    if (!g) return NULL;
    /* 5 s capture ring + grain bounds scaled to the host rate. */
    g->buf_len     = amb_scale_samples(G_BUF_SAMPLES, sr);
    g->grain_min   = amb_scale_samples(G_GRAIN_MIN_SAMPLES, sr);
    g->grain_max   = amb_scale_samples(G_GRAIN_MAX_SAMPLES, sr);
    g->max_scatter = amb_scale_samples(G_MAX_SCATTER_SAMP, sr);
    g->buf_L = (float*)calloc((size_t)g->buf_len, sizeof(float));
    g->buf_R = (float*)calloc((size_t)g->buf_len, sizeof(float));
    if (!g->buf_L || !g->buf_R) { granular_destroy(g); return NULL; }
    g->grain_size_0_1 = 0.5f;
    g->scatter_0_1 = 0.0f;
    lfo_init(&g->mod_lfo, (int)sr);
    lfo_set_rate_hz(&g->mod_lfo, 0.3f);
    g->mod_depth_cents = 0.0f;
    g->rng = 0x12345678u;
    g->samples_to_next = current_grain_length(g) / 3;   /* ~3x overlap = smoother */
    return g;
}

void granular_destroy(granular_t *g) {
    if (!g) return;
    free(g->buf_L);
    free(g->buf_R);
    free(g);
}

/* Clear the capture ring + kill active grains (RT-safe). Keeps params. */
void granular_reset(granular_t *g) {
    if (!g) return;
    memset(g->buf_L, 0, (size_t)g->buf_len * sizeof(float));
    memset(g->buf_R, 0, (size_t)g->buf_len * sizeof(float));
    g->write_pos = 0;
    g->samples_to_next = 0;
    for (int i = 0; i < G_MAX_GRAINS; i++) g->grains[i].active = 0;
}

void granular_set_grain_size(granular_t *g, float size_0_1) {
    if (!g) return;
    if (size_0_1 < 0.0f) size_0_1 = 0.0f;
    if (size_0_1 > 1.0f) size_0_1 = 1.0f;
    g->grain_size_0_1 = size_0_1;
}

void granular_set_scatter(granular_t *g, float scatter_0_1) {
    if (!g) return;
    if (scatter_0_1 < 0.0f) scatter_0_1 = 0.0f;
    if (scatter_0_1 > 1.0f) scatter_0_1 = 1.0f;
    g->scatter_0_1 = scatter_0_1;
}

/* Dilate: 0 = all grains forward, 1 = all reversed (a fraction reverses that
 * proportion). Decided per grain at spawn; existing grains finish their life, so
 * toggling crossfades over ~a grain length. */
void granular_set_reverse(granular_t *g, float amount_0_1) {
    if (!g) return;
    if (amount_0_1 < 0.0f) amount_0_1 = 0.0f;
    if (amount_0_1 > 1.0f) amount_0_1 = 1.0f;
    g->reverse_0_1 = amount_0_1;
}

void granular_set_mod_depth(granular_t *g, float depth_0_1) {
    if (!g) return;
    if (depth_0_1 < 0.0f) depth_0_1 = 0.0f;
    if (depth_0_1 > 1.0f) depth_0_1 = 1.0f;
    g->mod_depth_cents = depth_0_1 * 100.0f;
}

void granular_set_mod_rate_hz(granular_t *g, float hz) {
    if (!g) return;
    if (hz < 0.0f) hz = 0.0f;
    lfo_set_rate_hz(&g->mod_lfo, hz);
}

void granular_process(granular_t *g,
                      const float *in_l, const float *in_r,
                      float *out_l, float *out_r,
                      int frames) {
    if (!g || frames <= 0) return;

    const int buf_len = g->buf_len;
    const float mod_cents = g->mod_depth_cents;

    for (int n = 0; n < frames; n++) {
        /* Per-sample shared mod factor (same across all active grains). */
        float lfo_val = lfo_tick_sine(&g->mod_lfo);
        float pitch_mod = (mod_cents > 0.0f)
            ? fast_exp2f(lfo_val * mod_cents * (1.0f / 1200.0f))
            : 1.0f;
        /* 1. Write current input to capture buffer. */
        g->buf_L[g->write_pos] = in_l[n];
        g->buf_R[g->write_pos] = in_r[n];
        g->write_pos++;
        if (g->write_pos >= buf_len) g->write_pos = 0;

        /* 2. Grain scheduler — fire every length/2 samples (50% overlap). */
        g->samples_to_next--;
        if (g->samples_to_next <= 0) {
            spawn_grain(g);
            g->samples_to_next = current_grain_length(g) / 3;   /* ~3x overlap = smoother */
            if (g->samples_to_next < 1) g->samples_to_next = 1;
        }

        /* 3. Render active grains. */
        float sum_l = 0.0f, sum_r = 0.0f;
        for (int i = 0; i < G_MAX_GRAINS; i++) {
            grain_t *gr = &g->grains[i];
            if (!gr->active) continue;

            /* Linear-interp read at fractional position. */
            int pi = (int)gr->read_pos;
            float pf = gr->read_pos - (float)pi;
            int pi2 = pi + 1; if (pi2 >= buf_len) pi2 = 0;
            float yl = g->buf_L[pi] * (1.0f - pf) + g->buf_L[pi2] * pf;
            float yr = g->buf_R[pi] * (1.0f - pf) + g->buf_R[pi2] * pf;

            /* Envelope. Forward: Hann (smooth, overlap-adds to ~1.0). Reversed
             * (Dilate): a SWELL — amplitude rises across the grain then a quick
             * fade at the end, so it audibly blooms backward instead of just a
             * time-reversed Hann (which sounds barely reversed). */
            float phase = (float)gr->age / (float)gr->length;
            float env;
            if (gr->reversed)
                env = (phase < 0.85f) ? sinf (1.5707963f * (phase / 0.85f))
                                      : cosf (1.5707963f * ((phase - 0.85f) / 0.15f));
            else
                env = 0.5f * (1.0f - fast_cosf(TWO_PI * phase));

            sum_l += yl * env;
            sum_r += yr * env;

            /* Advance grain with shared LFO pitch wobble applied. */
            gr->read_pos += gr->read_step * pitch_mod;
            if (gr->read_pos >= (float)buf_len) gr->read_pos -= (float)buf_len;
            else if (gr->read_pos < 0.0f) gr->read_pos += (float)buf_len;
            gr->age++;
            if (gr->age >= gr->length) gr->active = 0;
        }

        out_l[n] = sum_l;
        out_r[n] = sum_r;
    }
}
