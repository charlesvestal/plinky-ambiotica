/* Shared, JUCE-free replication of the Ambiotica chain wiring.
 *
 * This mirrors PluginProcessor::processBlock + pushParamsToDsp +
 * updateLoopLength exactly, but takes already-created module pointers so it
 * is agnostic to the *_create() signatures (which change during the Phase 1
 * sample-rate refactor). Used by the golden-capture path and the parity test
 * so the wiring under test can never drift between the two.
 *
 * Header-only, static functions — include once per test translation unit.
 */
#ifndef AMBIOTICA_TEST_CHAIN_H
#define AMBIOTICA_TEST_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif
#include "looper.h"
#include "granular.h"
#include "microloop.h"
#include "reverb.h"
#ifdef __cplusplus
}
#endif

#include <math.h>

/* Mirrors the constants in PluginProcessor.cpp. */
#define AMBT_MAX_BLOCK     256
#define AMBT_BEATS_PER_BAR 4

/* Tempo-synced mod-rate beat divisions (slow -> fast), from the processor. */
static const float ambt_sync_beats[6] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };

typedef struct {
    /* 8 knobs (0..1). */
    float mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate;
    int   mod_shape;       /* 0=Sine,1=Warp,2=Sink */
    int   mod_sync;        /* tempo sync on/off */
    int   lofi;            /* lo-fi tails (reverb stretch) on/off */
    float loop_length_bars;
    float bpm;
} ambt_params;

static float ambt_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static int ambt_clampi(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Push the (constant) params onto the DSP modules — mirrors pushParamsToDsp.
 * Called every block like the processor does (the smoothing/crossfade state
 * machines depend on being re-poked each block). */
static void ambt_push_params(looper_t *l, granular_t *g, microloop_t *m,
                             reverb_t *r, const ambt_params *p) {
    looper_set_layer        (l, p->loop_layer);
    granular_set_grain_size (g, p->grain_size);
    granular_set_scatter    (g, p->scatter);
    granular_set_mod_depth  (g, p->mod_depth);
    microloop_set_hold      (m, p->micro_hold);
    reverb_set_decay        (r, p->decay);
    reverb_set_mod_depth    (r, p->mod_depth);
    reverb_set_mod_shape    (r, p->mod_shape);
    reverb_set_stretch      (r, p->lofi ? 1 : 0);

    float hz;
    if (p->mod_sync) {
        double bpm = p->bpm > 0.0f ? p->bpm : 120.0f;
        int idx = ambt_clampi((int)(p->mod_rate * 6.0f), 0, 5);
        hz = (float)bpm / (60.0f * ambt_sync_beats[idx]);
        reverb_set_mod_rate_hz (r, hz);
    } else {
        hz = 0.05f * expf(p->mod_rate * 5.075f);
        reverb_set_mod_rate    (r, p->mod_rate);
    }
    granular_set_mod_rate_hz (g, hz);
}

/* Mirrors updateLoopLength: bars + bpm + sample_rate -> looper length. */
static void ambt_update_loop_len(looper_t *l, const ambt_params *p, double sample_rate) {
    double bpm = p->bpm > 0.0f ? p->bpm : 120.0f;
    float bars = ambt_clampf(p->loop_length_bars, 0.5f, 8.0f);
    float loop_seconds = bars * (float)AMBT_BEATS_PER_BAR * 60.0f / (float)bpm;
    int   samples = (int)(loop_seconds * (float)sample_rate);
    looper_set_loop_len(l, samples);
}

/* Render `total` stereo frames through the chain, exactly as processBlock does
 * (chunked at AMBT_MAX_BLOCK). out_* must hold `total` frames; in/out separate. */
static void ambt_render_chain(looper_t *l, granular_t *g, microloop_t *m, reverb_t *r,
                              const ambt_params *p, double sample_rate,
                              const float *in_l, const float *in_r,
                              float *out_l, float *out_r, int total) {
    const float scatter   = p->scatter;
    const float cleanG    = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmerG  = scatter;
    const float mixTarget = p->mix;
    float mixCurrent = p->mix;   /* prepareToPlay seeds mixCurrent = mix */

    float dryL[AMBT_MAX_BLOCK],   dryR[AMBT_MAX_BLOCK];
    float loopL[AMBT_MAX_BLOCK],  loopR[AMBT_MAX_BLOCK];
    float granL[AMBT_MAX_BLOCK],  granR[AMBT_MAX_BLOCK];
    float microL[AMBT_MAX_BLOCK], microR[AMBT_MAX_BLOCK];
    float revInL[AMBT_MAX_BLOCK], revInR[AMBT_MAX_BLOCK];
    float wetL[AMBT_MAX_BLOCK],   wetR[AMBT_MAX_BLOCK];
    float layeredL[AMBT_MAX_BLOCK], layeredR[AMBT_MAX_BLOCK];

    for (int start = 0; start < total; start += AMBT_MAX_BLOCK) {
        const int n = (AMBT_MAX_BLOCK < total - start) ? AMBT_MAX_BLOCK : (total - start);

        /* Per-block: host transport sync + param push (mirrors processBlock). */
        ambt_update_loop_len(l, p, sample_rate);
        ambt_push_params(l, g, m, r, p);

        for (int i = 0; i < n; ++i) { dryL[i] = in_l[start + i]; dryR[i] = in_r[start + i]; }

        looper_process   (l, dryL, dryR, loopL, loopR, n);
        granular_process (g, loopL, loopR, granL, granR, n);

        for (int i = 0; i < n; ++i) {
            layeredL[i] = cleanG * loopL[i] + shimmerG * granL[i];
            layeredR[i] = cleanG * loopR[i] + shimmerG * granR[i];
        }

        microloop_process(m, dryL, dryR, microL, microR, n);

        for (int i = 0; i < n; ++i) {
            revInL[i] = dryL[i] + layeredL[i] + microL[i];
            revInR[i] = dryR[i] + layeredR[i] + microR[i];
        }

        reverb_process(r, revInL, revInR, wetL, wetR, n);

        const float c = 0.9989f, ic = 1.0f - c;
        float mm = mixCurrent;
        for (int i = 0; i < n; ++i) {
            mm = c * mm + ic * mixTarget;
            const float dryG = 1.0f - mm;
            const float wetBusL = layeredL[i] + microL[i] + wetL[i];
            const float wetBusR = layeredR[i] + microR[i] + wetR[i];
            float lv = dryG * dryL[i] + mm * wetBusL;
            float rv = dryG * dryR[i] + mm * wetBusR;
            lv = ambt_clampf(lv, -1.0f, 1.0f);
            rv = ambt_clampf(rv, -1.0f, 1.0f);
            out_l[start + i] = lv;
            out_r[start + i] = rv;
        }
        mixCurrent = mm;
    }
}

#endif /* AMBIOTICA_TEST_CHAIN_H */
