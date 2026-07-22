/* Full Ambiotica chain (all 7 modules), block-stateful.
 *
 * Faithful reproduction of PluginProcessor::processBlock's SIGNAL ORDER and key
 * makeups (loop-bed lift, scatter crossfade, drift regeneration feedback of the
 * post-harmony tail into the loop). Simplified vs the plugin: performance mutes
 * = 1, event-horizon duck / gravity / output-level omitted, micro makeup ~unity
 * (on-device fidelity tuning). All persistent state AND per-block scratch live
 * in fc_state so it can be called one 64-frame block at a time (the Plinky panel
 * path) with nothing large on the stack.
 */
#ifndef AMB_FULL_CHAIN_H
#define AMB_FULL_CHAIN_H

#include "looper.h"
#include "fast_math.h"
#include "granular.h"
#include "microloop.h"
#include "reverb.h"
#include "harmony.h"
#include "bloom.h"
#include "drift.h"
#include <math.h>
#include <string.h>

#define FC_BLK 64            /* = Plinky BLOCK_SIZE; keeps fc_state small (~5.5 KB) */
#define FC_BEATS_PER_BAR 4

/* Chain complexity ladder, to find what fits core0's ~2 ms budget on device.
 * Each level adds a module (cheapest -> most expensive):
 *   0 passthrough · 1 looper · 2 +reverb · 3 +bloom/drift · 4 +harmony(Spectra)
 *   5 +microloop · 6 +granular/regen (full). */
#ifndef AMB_CHAIN_LEVEL
#define AMB_CHAIN_LEVEL 6
#endif

typedef struct {
    float mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate;
    float bloom, drift_amt, spectra, ring;      /* spectra = harmony amount */
    float loop_length_bars, bpm;
    int   key, chord;                            /* Spectra chord: 0..4 = min/maj/sus4/5th/oct */
} full_params;

/* Persistent + scratch state. Zero-init then set mixCur = params.mix. */
typedef struct {
    float mixCur, driftFbCur, dcL, dcR, fbL, fbR;         /* persistent across blocks */
    float revFbL[FC_BLK], revFbR[FC_BLK]; int revFbN;
    /* per-block scratch (here, not on the stack) */
    float dryL[FC_BLK], dryR[FC_BLK], srcL[FC_BLK], srcR[FC_BLK];
    float loopL[FC_BLK], loopR[FC_BLK], granL[FC_BLK], granR[FC_BLK];
    float layL[FC_BLK], layR[FC_BLK], blL[FC_BLK], blR[FC_BLK];
    float micL[FC_BLK], micR[FC_BLK], rinL[FC_BLK], rinR[FC_BLK];
    float wetL[FC_BLK], wetR[FC_BLK], wbL[FC_BLK], wbR[FC_BLK];
} fc_state;

static const int FC_CHORD_SEMIS[5][4] = {
    {0,3,7,-1}, {0,4,7,-1}, {0,5,7,-1}, {0,7,-1,-1}, {0,12,-1,-1}   /* min maj sus4 5th oct */
};
static int fc_build_chord(int key, int chord, float* out) {
    if (key < 0) key = 0; if (key > 11) key = 11;
    if (chord < 0) chord = 0; if (chord > 4) chord = 4;
    const int root = 36 + key;                   /* C2 + key (matches buildChordFreqs) */
    int n = 0;
    for (int i = 0; i < 4; i++) { int s = FC_CHORD_SEMIS[chord][i]; if (s < 0) break;
        out[n++] = 440.0f * powf(2.0f, (float)(root + s - 69) / 12.0f); }
    return n;
}

static void fc_init(fc_state* st, float mix) { memset(st, 0, sizeof(*st)); st->mixCur = mix; }

static void fc_push_params(looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                           harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr) {
    double bpm = p->bpm > 0 ? p->bpm : 120.0;
    float bars = p->loop_length_bars; if (bars < 0.5f) bars = 0.5f; if (bars > 8) bars = 8;
    looper_set_loop_len(l, (int)(bars * FC_BEATS_PER_BAR * 60.0f / bpm * sr));
    looper_set_layer(l, p->loop_layer);
    granular_set_grain_size(g, p->grain_size);
    granular_set_scatter(g, p->scatter);
    granular_set_mod_depth(g, 0.0f);
    microloop_set_hold(m, p->micro_hold);
    bloom_set_amount(b, p->bloom * 0.15f);
    drift_set_amount(d, p->drift_amt);
    harmony_set_amount(h, p->spectra);
    harmony_set_ring(h, p->ring);
    { float f[HARMONY_MAX_VOICES]; int nv = fc_build_chord(p->key, p->chord, f); harmony_set_chord(h, f, nv); }
    reverb_set_decay(r, p->decay);
    reverb_set_mod_depth(r, p->mod_depth);
    reverb_set_mod_shape(r, 0);
    float hz = 0.05f * expf(p->mod_rate * 5.075f);
    reverb_set_mod_rate(r, p->mod_rate);
    granular_set_mod_rate_hz(g, hz);
    reverb_set_mod_rate_hz(r, hz);
}

/* Process ONE block of n (<= FC_BLK) frames. State persists in st across calls. */
static void fc_render_block(fc_state* st, looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                            harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                            const float* in_l, const float* in_r, float* out_l, float* out_r, int n) {
    if (n > FC_BLK) n = FC_BLK;
    const float kLoopBedMakeup = 1.9f;
    const float scatter  = p->scatter;
    const float cleanG   = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmerG = scatter;
    const float driftFbGain = 0.22f * p->drift_amt * (1.0f - 0.78f * p->decay);
    const float dcIC = 0.005f, fbIC = 0.02f;

    fc_push_params(l, g, m, r, h, b, d, p, sr);

    for (int i = 0; i < n; i++) {                        /* DC block */
        st->dcL += dcIC * (in_l[i] - st->dcL); st->dcR += dcIC * (in_r[i] - st->dcR);
        st->dryL[i] = in_l[i] - st->dcL; st->dryR[i] = in_r[i] - st->dcR;
    }
    for (int i = 0; i < n; i++) { st->srcL[i] = st->dryL[i]; st->srcR[i] = st->dryR[i]; }
#if AMB_CHAIN_LEVEL >= 6
    if (driftFbGain > 0.0f || st->driftFbCur > 1e-6f)   /* drift regeneration */
        for (int i = 0; i < n; i++) {
            st->driftFbCur += 0.0007f * (driftFbGain - st->driftFbCur);
            float fl = (i < st->revFbN) ? st->revFbL[i] : 0.f, fr = (i < st->revFbN) ? st->revFbR[i] : 0.f;
            st->fbL += fbIC * (fl - st->fbL); st->fbR += fbIC * (fr - st->fbR);
            st->srcL[i] += st->driftFbCur * fast_tanhf(st->fbL); st->srcR[i] += st->driftFbCur * fast_tanhf(st->fbR);
        }
#endif
#if AMB_CHAIN_LEVEL >= 1
    looper_process(l, st->srcL, st->srcR, st->loopL, st->loopR, n);
    for (int i = 0; i < n; i++) { st->loopL[i] *= kLoopBedMakeup; st->loopR[i] *= kLoopBedMakeup; }
#else
    for (int i = 0; i < n; i++) { st->loopL[i] = st->loopR[i] = 0.f; }
#endif
#if AMB_CHAIN_LEVEL >= 6
    granular_process(g, st->loopL, st->loopR, st->granL, st->granR, n);
#else
    for (int i = 0; i < n; i++) { st->granL[i] = st->granR[i] = 0.f; }
#endif
    for (int i = 0; i < n; i++) { st->layL[i] = cleanG * st->loopL[i] + shimmerG * st->granL[i];
                                  st->layR[i] = cleanG * st->loopR[i] + shimmerG * st->granR[i]; }
#if AMB_CHAIN_LEVEL >= 3
    bloom_process(b, st->srcL, st->srcR, st->blL, st->blR, n);
#else
    for (int i = 0; i < n; i++) { st->blL[i] = st->srcL[i]; st->blR[i] = st->srcR[i]; }
#endif
#if AMB_CHAIN_LEVEL >= 5
    microloop_process(m, st->blL, st->blR, st->micL, st->micR, n);
#else
    for (int i = 0; i < n; i++) { st->micL[i] = st->micR[i] = 0.f; }
#endif
    for (int i = 0; i < n; i++) { st->rinL[i] = st->blL[i] + st->layL[i] + st->micL[i];
                                  st->rinR[i] = st->blR[i] + st->layR[i] + st->micR[i]; }
#if AMB_CHAIN_LEVEL >= 2
    reverb_process(r, st->rinL, st->rinR, st->wetL, st->wetR, n);
#else
    for (int i = 0; i < n; i++) { st->wetL[i] = st->rinL[i]; st->wetR[i] = st->rinR[i]; }
#endif
#if AMB_CHAIN_LEVEL >= 4
    harmony_process(h, st->wetL, st->wetR, st->wetL, st->wetR, n);          /* Spectra on reverb tail */
#endif
    for (int i = 0; i < n; i++) { st->revFbL[i] = st->wetL[i]; st->revFbR[i] = st->wetR[i]; } st->revFbN = n;
    for (int i = 0; i < n; i++) { st->wbL[i] = st->layL[i] + st->micL[i] + st->wetL[i];
                                  st->wbR[i] = st->layR[i] + st->micR[i] + st->wetR[i]; }
#if AMB_CHAIN_LEVEL >= 3
    drift_process(d, st->wbL, st->wbR, st->wbL, st->wbR, n);                /* wet-bus detune */
#endif

    const float c = 0.9989f, ic = 1.0f - c; float mm = st->mixCur;
    for (int i = 0; i < n; i++) {
        mm = c * mm + ic * p->mix; float dg = 1.0f - mm;
        float lv = dg * st->dryL[i] + mm * st->wbL[i], rv = dg * st->dryR[i] + mm * st->wbR[i];
        lv = lv < -1 ? -1 : lv > 1 ? 1 : lv; rv = rv < -1 ? -1 : rv > 1 ? 1 : rv;
        out_l[i] = lv; out_r[i] = rv;
    }
    st->mixCur = mm;
}

/* Harness convenience: render `total` frames in FC_BLK chunks (one fc_state). */
static void fc_render(looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                      harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                      const float* in_l, const float* in_r, float* out_l, float* out_r, int total) {
    static fc_state st;                       /* static: keep ~22 KB off the stack */
    fc_init(&st, p->mix);
    for (int start = 0; start < total; start += FC_BLK) {
        int n = (FC_BLK < total - start) ? FC_BLK : (total - start);
        fc_render_block(&st, l, g, m, r, h, b, d, p, sr, in_l + start, in_r + start, out_l + start, out_r + start, n);
    }
}
#endif
