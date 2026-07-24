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
#include "dattorro.h"
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

#ifdef AMB_STAGE_TIMING   /* per-stage core1 profiling (time_us from the SDK); panel prints it */
static unsigned int g_stage_us[7];   /* 0 loop 1 gran 2 micro 3 reverb 4 harmony 5 mix */
static unsigned int g_stage_n;
#endif

typedef struct {
    float mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate;
    float bloom, drift_amt, spectra, ring;      /* spectra = harmony amount */
    float loop_length_bars, micro_bars, bpm;    /* micro_bars = Satellite micro-loop length */
    int   key, chord;                            /* Spectra chord: 0..4 = min/maj/sus4/5th/oct */
    float gravity;                               /* 0..1 slow tremolo "collapse" throb (post-mix) */
    float horizon;                               /* Event Horizon: 1 = full sustain, <1 drains loop/micro/wet */
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
    full_params last_pushed;   /* memoize: only re-push params when they change */
    int have_pushed;
    unsigned push_ctr;         /* throttles the coefficient re-push during slow macro ramps */
    float br_holdL, br_holdR;  /* built-in reverb: last 16 kHz wet, for 32 kHz upsample */
    float br_peak;             /* DEBUG: reverb wet peak this block */
    float gravPhase;           /* Gravity tremolo LFO phase */
    dattorro_t* dat;           /* Dattorro plate (AMB_DATTORRO); created by the panel / fc_render */
} fc_state;

/* 3-voice chord (reduced from the plugin's 5). The plugin's minor is
 * {0,3,7,12,19} = C2 Eb2 G2 C3 G3 — the AUDIBLE character lives in the upper
 * voices (C3/G3); the port used to keep the lowest {0,3,7} = C2 Eb2 G2, a
 * sub-bass drone you feel but don't hear as a chord. Voice the 3 resonators one
 * octave up so the full triad sits in an audible, lush register (C3..C5). */
static const int FC_CHORD_SEMIS[5][4] = {
    {12,15,19,-1},   /* min:  C3 Eb3 G3 */
    {12,16,19,-1},   /* maj:  C3 E3  G3 */
    {12,17,19,-1},   /* sus4: C3 F3  G3 */
    {12,19,24,-1},   /* 5th:  C3 G3  C4 */
    {12,24,36,-1}    /* oct:  C3 C4  C5 */
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
    looper_set_layer(l, p->loop_layer);   /* Event Horizon drains via the deriveStages lerp (loopLayer->0.08) */
    granular_set_grain_size(g, p->grain_size);
    granular_set_scatter(g, p->scatter);
    granular_set_mod_depth(g, 0.0f);
    microloop_set_hold(m, p->micro_hold);
    { float mb = p->micro_bars > 0.01f ? p->micro_bars : 0.25f;      /* Satellite micro length */
      int mlen = (int)(mb * FC_BEATS_PER_BAR * 60.0f / bpm * sr); if (mlen < 1) mlen = 1;
      microloop_set_loop_len(m, mlen); }
    bloom_set_amount(b, p->bloom * 0.15f);
    drift_set_amount(d, p->drift_amt);
    harmony_set_amount(h, p->spectra);
    harmony_set_ring(h, p->ring);
    /* chord (fc_build_chord uses powf per voice) is rebuilt in fc_render_block ONLY on
     * key/chord change — not here, which runs on every param move (Flux, Gravity ramp). */
    reverb_set_decay(r, p->decay);
    reverb_set_mod_depth(r, p->mod_depth);
    reverb_set_mod_shape(r, 0);
    float hz = 0.05f * expf(p->mod_rate * 5.075f);
    reverb_set_mod_rate(r, p->mod_rate);
    granular_set_mod_rate_hz(g, hz);
    reverb_set_mod_rate_hz(r, hz);
}

#ifdef AMB_BUILTIN_REVERB
/* Plinky NATIVE reverb via the firmware do_reverb() call, in place of our modal
 * comb reverb. Fixes the sparse-3-comb "broken piano" under Flux AND frees the
 * ~60 KB SRAM the modal reverb used (do_reverb uses the pre-allocated
 * mix_buffers.reverbbuf). do_reverb runs at 16 kHz (half rate) and ACCUMULATES
 * int wet in the ±32768 audio scale; we decimate 32->16k in, upsample 16->32k
 * out, and stay in float [-1,1]. The *_q7/q8 mappings + shimmer are the tunables. */
static void fc_builtin_reverb(fc_state* st, const float* inL, const float* inR,
                              float* outL, float* outR, int n, const full_params* p) {
    /* Tail -> reverb TAIL LENGTH, matched to the plugin. Measured in the Plinky sim
     * (do_reverb RT60): size_q7 alone only spans ~0.6..2 s, but the tail is really
     * governed by feedback. Base FEEDBACK=12 (mix preset, resolved by the do_fx
     * warmup) gives ~1.2 s at Tail=0 (plugin ~1.34 s); reverb_extra_fb_gain_q8 0..240
     * then extends RT60 to ~5.6 s (all finite, no runaway), covering the plugin's
     * ~1.3..5 s across most of the Tail range. Room size is fixed; the ^0.8 curve
     * fits the plugin's accelerating RT60(Tail). Tail is fed pre-smoothed (fx_sm),
     * and the fb step is <1/255 per block, so no click. */
    const int size_q7 = 60;
    float tail01 = (p->decay - 0.30f) * (1.0f / 0.70f);   /* undo Tail->decay map to 0..1 */
    if (tail01 < 0.f) tail01 = 0.f; else if (tail01 > 1.f) tail01 = 1.f;
    /* Tail -> native feedback. Measured in the sim: extra_fb 0..255 @ size 60 gives
     * RT60 ~1.2..7 s (finite, no runaway; raising the base mix FEEDBACK does NOT
     * lengthen it, so extra_fb is the only lever). ^0.7 curve for longer mid tails. */
    reverb_extra_fb_gain_q8 = (int)(255.0f * powf(tail01, 0.7f) + 0.5f);
    reverb_extra_shimmer    = 0;
    /* do_reverb expects a SEND-level input (stock reverbsend ~= mono*reverb_send>>8,
     * ~1/10 full scale); feeding full-scale ±32767 slammed the ceiling. Attenuate
     * in; scale out with makeup. Both TUNABLE via the debug REVLVL readout. */
    /* kOut sets the wet level. The Spectra harmony resonators (fb up to 0.993, Q~143)
     * saturate and "blow out" once the wet feeding them exceeds ~0.15 RMS (measured
     * against the plugin's own harmony.c) — so keep the wet peak ~0.30 (RMS ~0.10),
     * comfortably under that, which also tracks the plugin's reverb-send level. */
    const float kIn = 3000.0f, kOut = 1.6f / 32768.0f;
    float rawmax = 0.f;
    for (int i = 0; i < n; i += 2) {
        float dL = 0.5f * (inL[i] + (i + 1 < n ? inL[i + 1] : inL[i]));   /* 32k -> 16k decimate */
        float dR = 0.5f * (inR[i] + (i + 1 < n ? inR[i + 1] : inR[i]));
        float cl = dL < -1.f ? -1.f : dL > 1.f ? 1.f : dL;
        float cr = dR < -1.f ? -1.f : dR > 1.f ? 1.f : dR;
        int wl = 0, wr = 0;
        do_reverb((int)(cl * kIn), (int)(cr * kIn), size_q7, &wl, &wr);   /* accumulates */
        float wL = (float)wl * kOut, wR = (float)wr * kOut;
        { float a = wL < 0.f ? -wL : wL; if (a > rawmax) rawmax = a; }    /* DEBUG raw peak */
        if (wL > 1.5f) wL = 1.5f; else if (wL < -1.5f) wL = -1.5f;        /* safety clamp */
        if (wR > 1.5f) wR = 1.5f; else if (wR < -1.5f) wR = -1.5f;
        outL[i] = 0.5f * (st->br_holdL + wL);   /* 16k -> 32k linear upsample */
        outR[i] = 0.5f * (st->br_holdR + wR);
        if (i + 1 < n) { outL[i + 1] = wL; outR[i + 1] = wR; }
        st->br_holdL = wL; st->br_holdR = wR;
    }
    st->br_peak = rawmax;   /* DEBUG */
}
#endif

/* Process ONE block of n (<= FC_BLK) frames. State persists in st across calls. */
static void fc_render_block(fc_state* st, looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                            harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                            const float* in_l, const float* in_r, float* out_l, float* out_r, int n) {
    if (n > FC_BLK) n = FC_BLK;
    const float kLoopBedMakeup = 1.9f;   /* plugin value (processBlock) */
    const float scatter  = p->scatter;
    const float cleanG   = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmerG = 0.55f + 0.30f * scatter;   /* 0.55..0.85 — plugin parity: a grain
                                                         floor so the pitched (oct/5th) grains
                                                         blend as an in-key bed, not exposed blips */
    const float driftFbGain = 0.22f * p->drift_amt * (1.0f - 0.78f * p->decay) * (1.0f - 0.50f * p->spectra);
    /* One-pole coefficients from the plugin's cutoffs, at the host rate. These
     * were hardcoded (0.005 / 0.02 ≈ 25 Hz / 103 Hz); the regen LP at ~103 Hz
     * instead of the plugin's 2500 Hz fed back a near-sub-bass-only tail, so the
     * Drift-regen wash spiralled DOWN in pitch (the "falling" heard with Flux up
     * — absent in the plugin). Match the plugin: 5 Hz DC blocker, 2500 Hz regen LP. */
    const float dcIC = 1.0f - expf(-6.2831853f *    5.0f / (float) sr);
    const float fbIC = 1.0f - expf(-6.2831853f * 2500.0f / (float) sr);

    /* Only re-push params (incl. the powf chord build + expf mod rate) when they
     * actually change — otherwise the audio thread wastes transcendentals every
     * block on constant values. */
#ifdef AMB_STAGE_TIMING
    unsigned int _tp = time_us();
#endif
    {
        int keyChord = !st->have_pushed || p->key != st->last_pushed.key || p->chord != st->last_pushed.chord;
        int changed  = !st->have_pushed || memcmp(p, &st->last_pushed, sizeof(full_params)) != 0;
        /* fc_push_params re-runs ~18 setters (~70us). The macro ramps (Gravity/Flux)
         * change a param every 2ms block for ~2s, stacking that push onto an already
         * tight budget every block -> the "G! only while Gravity morphs" glitch. Throttle
         * continuous changes to every 4th block (~8ms — inaudible for these slow morphs,
         * and each module smooths its target per-sample anyway). Key/chord changes are
         * discrete and user-triggered, so those still push immediately (no chord lag). */
        if (changed && (keyChord || (st->push_ctr & 3u) == 0u)) {
            fc_push_params(l, g, m, r, h, b, d, p, sr);
            if (keyChord) {
                float f[HARMONY_MAX_VOICES]; int nv = fc_build_chord(p->key, p->chord, f);   /* powf per voice — key/chord only */
                harmony_set_chord(h, f, nv);
            }
            st->last_pushed = *p;
            st->have_pushed = 1;
        }
        st->push_ctr++;
    }
#ifdef AMB_STAGE_TIMING
    g_stage_us[6] += time_us() - _tp;   /* push (incl. re-fire cost during a Gravity ramp) */
#endif

#ifdef AMB_STAGE_TIMING
    unsigned int _tl = time_us();
    #define STG(k) do { unsigned int _tn = time_us(); g_stage_us[k] += _tn - _tl; _tl = _tn; } while (0)
#else
    #define STG(k)
#endif

    /* Event Horizon: horizon 1 = full sustain; lower DRAINS the engine (a global "let
     * go"). The plugin actively scales the whole loop buffer (looper_leak), but that
     * scans the entire loop window every block — far too expensive on the RP2350 (it
     * spiked the DSP budget and buzzed). Here the drain is done cheaply by pulling the
     * loop + micro FEEDBACK down (see fc_push_params) so the buffers decay, plus the
     * wet-tail duck below. */
    const float horizonClear = p->horizon >= 1.0f ? 0.0f : 1.0f - p->horizon;   /* 0..1 */

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
    STG(0);   /* looper + drift regen */
#if AMB_CHAIN_LEVEL >= 6
    granular_process(g, st->loopL, st->loopR, st->granL, st->granR, n);
#else
    for (int i = 0; i < n; i++) { st->granL[i] = st->granR[i] = 0.f; }
#endif
    STG(1);   /* granular */
    for (int i = 0; i < n; i++) { st->layL[i] = cleanG * st->loopL[i] + shimmerG * st->granL[i];
                                  st->layR[i] = cleanG * st->loopR[i] + shimmerG * st->granR[i]; }
#if AMB_CHAIN_LEVEL >= 3
    bloom_process(b, st->srcL, st->srcR, st->blL, st->blR, n);
#else
    for (int i = 0; i < n; i++) { st->blL[i] = st->srcL[i]; st->blR[i] = st->srcR[i]; }
#endif
#if AMB_CHAIN_LEVEL >= 5
    microloop_process(m, st->blL, st->blR, st->micL, st->micR, n);
    /* Satellite makeup (plugin processBlock): the microloop's own out-gain is
     * 0.25*sqrt(hold), far quieter than the loop bed. Invert toward a flat ~1.1
     * low/mid level (jlimit 1..14), taper to unity approaching freeze, then a -25%
     * overall trim. Applied before the reverb send + wet sum. */
    { float mh = p->micro_hold < 0.f ? 0.f : (p->micro_hold > 1.f ? 1.f : p->micro_hold);
      float og = 0.25f * sqrtf(mh > 1.0e-4f ? mh : 1.0e-4f);
      float mk = 1.1f / og; if (mk < 1.f) mk = 1.f; else if (mk > 14.f) mk = 14.f;
      float tp = (mh - 0.7f) * (1.0f / 0.3f); if (tp < 0.f) tp = 0.f; else if (tp > 1.f) tp = 1.f;
      mk += (1.0f - mk) * tp; mk *= 0.75f;
      for (int i = 0; i < n; i++) { st->micL[i] *= mk; st->micR[i] *= mk; } }
#else
    for (int i = 0; i < n; i++) { st->micL[i] = st->micR[i] = 0.f; }
#endif
    /* Micro reverb send: full for short/mid delays, reduced only in the held-pad zone
     * (plugin). rin = bloom + layered + microRevSend*micro; the wet bus below uses the
     * full micro. */
    { float mp = (p->micro_hold - 0.80f) * (1.0f / 0.15f); if (mp < 0.f) mp = 0.f; else if (mp > 1.f) mp = 1.f;
      float microRevSend = 1.0f - 0.45f * mp;
      for (int i = 0; i < n; i++) { st->rinL[i] = st->blL[i] + st->layL[i] + microRevSend * st->micL[i];
                                    st->rinR[i] = st->blR[i] + st->layR[i] + microRevSend * st->micR[i]; } }
    STG(2);   /* bloom + microloop + makeup */
#if defined(AMB_DATTORRO)
    { float t = (p->decay - 0.30f) * (1.0f / 0.70f); if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
      dattorro_set_decay(st->dat, t);                       /* Tail -> tail length (tracks the plugin) */
      dattorro_set_mod(st->dat, 0.25f + 0.75f * p->mod_depth); /* Flux + a floor so the tank always shimmers */
      dattorro_set_damp(st->dat, 0.22f); }                  /* brighter/shinier tail = lusher */
    dattorro_process(st->dat, st->rinL, st->rinR, st->wetL, st->wetR, n);   /* Dattorro plate */
    { float pk = 0.f; for (int i = 0; i < n; i++) { float a = st->wetL[i] < 0 ? -st->wetL[i] : st->wetL[i]; if (a > pk) pk = a; } st->br_peak = pk; }
#elif defined(AMB_BUILTIN_REVERB)
    fc_builtin_reverb(st, st->rinL, st->rinR, st->wetL, st->wetR, n, p);   /* Plinky native reverb */
#elif AMB_CHAIN_LEVEL >= 2
    reverb_process(r, st->rinL, st->rinR, st->wetL, st->wetR, n);
#else
    for (int i = 0; i < n; i++) { st->wetL[i] = st->rinL[i]; st->wetR[i] = st->rinR[i]; }
#endif
    STG(3);   /* reverb (Dattorro) */
    if (horizonClear > 0.001f) {                 /* Event Horizon ducks the reverb wash (1..0.15) */
        float wetTailGain = 1.0f - 0.85f * horizonClear;
        for (int i = 0; i < n; i++) { st->wetL[i] *= wetTailGain; st->wetR[i] *= wetTailGain; }
    }
    /* Drift-regen tap = PRE-harmony wet (reverb wash only). The plugin taps POST-harmony
     * so the in-key chord recirculates; with the port's hotter Dattorro that becomes a
     * TUNED feedback runaway at the chord pitches — the "feedback that changes when you
     * change chords". Stashing the pre-chord wash keeps grains/reverb roughly in key
     * without the chord feeding its own excitation. */
    for (int i = 0; i < n; i++) { st->revFbL[i] = st->wetL[i]; st->revFbR[i] = st->wetR[i]; } st->revFbN = n;
#if AMB_CHAIN_LEVEL >= 4
    harmony_process(h, st->wetL, st->wetR, st->wetL, st->wetR, n);          /* add the chord (audible mix only) */
#endif
    for (int i = 0; i < n; i++) { st->wbL[i] = st->layL[i] + st->micL[i] + st->wetL[i];
                                  st->wbR[i] = st->layR[i] + st->micR[i] + st->wetR[i]; }
    STG(4);   /* harmony (Spectra) */
#if AMB_CHAIN_LEVEL >= 3
    drift_process(d, st->wbL, st->wbR, st->wbL, st->wbR, n);                /* wet-bus detune */
#endif

    /* Equal-power dry/wet crossfade (plugin): 50% keeps full loudness vs a linear
     * blend's -6 dB dip. Gains computed ONCE per block — mix is already smoothed
     * upstream (fx_sm), and per-sample fast_cosf/sinf here was over the core1 budget. */
    st->mixCur += 0.10f * (p->mix - st->mixCur);
    const float mmix = st->mixCur;
    const float dg = fast_cosf(mmix * 1.5707963f), wg = fast_sinf(mmix * 1.5707963f);
    /* Gravity: slow ~0.30 Hz tremolo throb (post-mix), per-sample only when engaged. */
    const float gravAmt   = p->gravity < 0.f ? 0.f : (p->gravity > 1.f ? 1.f : p->gravity);
    const float tremInc   = 6.2831853f * 0.30f / (float) sr;
    const float tremDepth = 0.40f * gravAmt;
    float gph = st->gravPhase;
    for (int i = 0; i < n; i++) {
        float lv = dg * st->dryL[i] + wg * st->wbL[i], rv = dg * st->dryR[i] + wg * st->wbR[i];
        if (gravAmt > 0.001f) {                          /* tremolo throb */
            gph += tremInc; if (gph > 6.2831853f) gph -= 6.2831853f;
            float trem = 1.0f - tremDepth * (0.5f - 0.5f * fast_cosf(gph));
            lv *= trem; rv *= trem;
        }
#ifdef FC_SOFT_CLIP
        lv = fast_tanhf(lv); rv = fast_tanhf(rv);   /* port soft-clip (hot poly); plugin hard-clips */
#else
        lv = lv < -1 ? -1 : lv > 1 ? 1 : lv; rv = rv < -1 ? -1 : rv > 1 ? 1 : rv;
#endif
        out_l[i] = lv; out_r[i] = rv;
    }
    st->gravPhase = gph;
    STG(5);   /* drift + mix/output */
#ifdef AMB_STAGE_TIMING
    g_stage_n++;
#endif
    #undef STG
}

/* Harness convenience: render `total` frames in FC_BLK chunks (one fc_state). */
static void fc_render(looper_t* l, granular_t* g, microloop_t* m, reverb_t* r,
                      harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                      const float* in_l, const float* in_r, float* out_l, float* out_r, int total) {
    static fc_state st;                       /* static: keep ~22 KB off the stack */
    fc_init(&st, p->mix);
#if defined(AMB_DATTORRO)
    if (!st.dat) st.dat = dattorro_create(sr);
#endif
    for (int start = 0; start < total; start += FC_BLK) {
        int n = (FC_BLK < total - start) ? FC_BLK : (total - start);
        fc_render_block(&st, l, g, m, r, h, b, d, p, sr, in_l + start, in_r + start, out_l + start, out_r + start, n);
    }
}
#endif
