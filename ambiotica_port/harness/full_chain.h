/* Full Ambiotica chain (all 7 modules) for the port harness.
 *
 * Faithful reproduction of PluginProcessor::processBlock's SIGNAL ORDER and the
 * key makeups (loop-bed lift, scatter crossfade, drift regeneration feedback of
 * the post-harmony tail back into the loop). Deliberately simplified vs the
 * plugin: performance mutes = 1, event-horizon duck / gravity coloration /
 * output-level trim omitted, micro makeup ~unity. Those are on-device fidelity
 * tuning, not part of the memory/feasibility question this harness answers.
 */
#ifndef AMB_FULL_CHAIN_H
#define AMB_FULL_CHAIN_H

#include "looper.h"
#include "granular.h"
#include "microloop.h"
#include "reverb.h"
#include "harmony.h"
#include "bloom.h"
#include "drift.h"
#include <math.h>
#include <string.h>

#define FC_BLK 256
#define FC_BEATS_PER_BAR 4

typedef struct {
    float mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate;
    float bloom, drift_amt, spectra, ring;      /* spectra = harmony amount */
    float loop_length_bars, bpm;
    int   key, chord;                            /* Spectra chord: 0..4 = min/maj/sus4/5th/oct */
} full_params;

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

static void fc_render(looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                      harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                      const float* in_l, const float* in_r, float* out_l, float* out_r, int total) {
    const float kLoopBedMakeup = 1.9f;
    const float scatter  = p->scatter;
    const float cleanG   = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmerG = scatter;
    const float driftFbGain = 0.22f * p->drift_amt * (1.0f - 0.78f * p->decay);
    float mixCur = p->mix, driftFbCur = 0.f, dcL = 0, dcR = 0, fbL = 0, fbR = 0;
    const float dcIC = 0.005f, fbIC = 0.02f;

    float dryL[FC_BLK], dryR[FC_BLK], srcL[FC_BLK], srcR[FC_BLK];
    float loopL[FC_BLK], loopR[FC_BLK], granL[FC_BLK], granR[FC_BLK];
    float layL[FC_BLK], layR[FC_BLK], blL[FC_BLK], blR[FC_BLK];
    float micL[FC_BLK], micR[FC_BLK], rinL[FC_BLK], rinR[FC_BLK];
    float wetL[FC_BLK], wetR[FC_BLK], wbL[FC_BLK], wbR[FC_BLK];
    float revFbL[FC_BLK], revFbR[FC_BLK]; int revFbN = 0;
    memset(revFbL, 0, sizeof revFbL); memset(revFbR, 0, sizeof revFbR);

    for (int start = 0; start < total; start += FC_BLK) {
        int n = (FC_BLK < total - start) ? FC_BLK : (total - start);
        fc_push_params(l, g, m, r, h, b, d, p, sr);

        for (int i = 0; i < n; i++) {                    /* DC block */
            dcL += dcIC * (in_l[start + i] - dcL); dcR += dcIC * (in_r[start + i] - dcR);
            dryL[i] = in_l[start + i] - dcL; dryR[i] = in_r[start + i] - dcR;
        }
        for (int i = 0; i < n; i++) { srcL[i] = dryL[i]; srcR[i] = dryR[i]; }
        if (driftFbGain > 0.0f || driftFbCur > 1e-6f)   /* drift regeneration */
            for (int i = 0; i < n; i++) {
                driftFbCur += 0.0007f * (driftFbGain - driftFbCur);
                float fl = (i < revFbN) ? revFbL[i] : 0.f, fr = (i < revFbN) ? revFbR[i] : 0.f;
                fbL += fbIC * (fl - fbL); fbR += fbIC * (fr - fbR);
                srcL[i] += driftFbCur * tanhf(fbL); srcR[i] += driftFbCur * tanhf(fbR);
            }
        looper_process(l, srcL, srcR, loopL, loopR, n);
        for (int i = 0; i < n; i++) { loopL[i] *= kLoopBedMakeup; loopR[i] *= kLoopBedMakeup; }
        granular_process(g, loopL, loopR, granL, granR, n);
        for (int i = 0; i < n; i++) { layL[i] = cleanG * loopL[i] + shimmerG * granL[i];
                                      layR[i] = cleanG * loopR[i] + shimmerG * granR[i]; }
        bloom_process(b, srcL, srcR, blL, blR, n);
        microloop_process(m, blL, blR, micL, micR, n);
        for (int i = 0; i < n; i++) { rinL[i] = blL[i] + layL[i] + micL[i];
                                      rinR[i] = blR[i] + layR[i] + micR[i]; }
        reverb_process(r, rinL, rinR, wetL, wetR, n);
        harmony_process(h, wetL, wetR, wetL, wetR, n);              /* Spectra on reverb tail */
        for (int i = 0; i < n; i++) { revFbL[i] = wetL[i]; revFbR[i] = wetR[i]; } revFbN = n;
        for (int i = 0; i < n; i++) { wbL[i] = layL[i] + micL[i] + wetL[i];
                                      wbR[i] = layR[i] + micR[i] + wetR[i]; }
        drift_process(d, wbL, wbR, wbL, wbR, n);                    /* wet-bus detune */

        const float c = 0.9989f, ic = 1.0f - c; float mm = mixCur;
        for (int i = 0; i < n; i++) {
            mm = c * mm + ic * p->mix; float dg = 1.0f - mm;
            float lv = dg * dryL[i] + mm * wbL[i], rv = dg * dryR[i] + mm * wbR[i];
            lv = lv < -1 ? -1 : lv > 1 ? 1 : lv; rv = rv < -1 ? -1 : rv > 1 ? 1 : rv;
            out_l[start + i] = lv; out_r[start + i] = rv;
        }
        mixCur = mm;
    }
}
#endif
