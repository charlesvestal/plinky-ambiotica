/* Ambiotica signal chain, processed one 64-frame block at a time.
 *
 * Reconstructs the ambiotica-plugin processBlock signal order for the Plinky panel:
 *
 *   in -> DC block -> Flux regen -> LOOPER bed -> GRANULAR scatter -> layer mix
 *      -> BLOOM swell -> MICROLOOP (Satellite) -> REVERB (Dattorro plate)
 *      -> Event Horizon wet-duck -> HARMONY (Spectra chord) -> DRIFT detune
 *      -> equal-power dry/wet mix -> soft clip.
 *
 * The two macro gestures (Gravity, Event Horizon) are applied upstream in the panel
 * by lerping many stage params at once (mirrors MacroMap::deriveStages); they arrive
 * here as ordinary param values, plus the Gravity tremolo and Event Horizon wet-duck
 * applied inline below.
 *
 * All persistent state and per-block scratch live in fc_state, so a block renders with
 * nothing large on the stack (the panel calls one 64-frame block at a time).
 */
#ifndef AMB_FULL_CHAIN_H
#define AMB_FULL_CHAIN_H

#include "looper.h"
#include "fast_math.h"
#include "granular.h"
#include "microloop.h"
#include "harmony.h"
#include "bloom.h"
#include "drift.h"
#include "dattorro.h"
#include <math.h>
#include <string.h>

#define FC_BLK 64            /* = Plinky BLOCK_SIZE; keeps fc_state small (~5.5 KB) */
#define FC_BEATS_PER_BAR 4

/* AMB_PROFILE (set by amalgamate.sh via `AMB_PROFILE=1 sh amalgamate.sh`) turns on
 * per-stage core1 timing using the SDK's time_us(); the panel prints the averages. Never
 * defined for the desktop harness (no time_us there). */
#ifdef AMB_PROFILE
static unsigned int g_stage_us[7];   /* 0 loop 1 gran 2 mic 3 rev 4 harm 5 mix 6 push */
static unsigned int g_stage_n;
#endif

typedef struct {
    float mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate;
    float bloom, drift_amt, spectra, ring;      /* spectra = harmony amount */
    float loop_length_bars, micro_bars, bpm;    /* micro_bars = Satellite micro-loop length */
    int   key, chord;                            /* chord = mode: 0..4 = Ion/Aeol/Dor/Lyd/Mixo (sets the Spectra tonic) */
    float gravity;                               /* Gravity macro amount; also a post-mix tremolo here */
    float horizon;                               /* Event Horizon: 1 = full sustain, <1 drains loop/micro/wet */
} full_params;

/* Persistent + per-block scratch state. Zero-init then set mixCur = params.mix. */
typedef struct {
    float mixCur, driftFbCur, dcL, dcR, fbL, fbR;         /* persistent across blocks */
    float revFbL[FC_BLK], revFbR[FC_BLK]; int revFbN;     /* pre-harmony wet, tapped for drift regen */
    /* per-block scratch (here, not on the stack) */
    float dryL[FC_BLK], dryR[FC_BLK], srcL[FC_BLK], srcR[FC_BLK];
    float loopL[FC_BLK], loopR[FC_BLK], granL[FC_BLK], granR[FC_BLK];
    float layL[FC_BLK], layR[FC_BLK], blL[FC_BLK], blR[FC_BLK];
    float micL[FC_BLK], micR[FC_BLK], rinL[FC_BLK], rinR[FC_BLK];
    float wetL[FC_BLK], wetR[FC_BLK], wbL[FC_BLK], wbR[FC_BLK];
    full_params last_pushed;   /* memoize: only re-push params when they change */
    int have_pushed;
    unsigned push_ctr;         /* throttles the coefficient re-push during slow macro ramps */
    float gravPhase;           /* Gravity tremolo LFO phase */
    dattorro_t* dat;           /* Dattorro plate; created by the panel / fc_render */
} fc_state;

/* Spectra wash = the selected MODE's tonic chord, as semitone offsets above the key
 * root, voiced ~C3 so it sits in an audible register. `chord` is the mode index
 * (0 Ionian/major, 1 Aeolian/minor, 2 Dorian, 3 Lydian, 4 Mixolydian). Dorian and
 * Mixolydian get a characteristic colour tone (nat-6 / b7); the rest are plain tonic
 * triads — each mode's full character lives on the play surface (its scale). */
static const int FC_CHORD_SEMIS[5][4] = {
    {12,16,19,-1},   /* Ionian / major    -> C3 E3  G3 */
    {12,15,19,-1},   /* Aeolian / minor   -> C3 Eb3 G3 */
    {12,15,21,-1},   /* Dorian            -> C3 Eb3 A3  (minor + nat 6) */
    {12,16,19,-1},   /* Lydian (major)    -> C3 E3  G3 */
    {12,16,22,-1}    /* Mixolydian        -> C3 E3  Bb3 (major + b7) */
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

/* Push all param-derived coefficients into the modules. Called only when params change
 * (and throttled during ramps — see fc_render_block), since it runs many setters. The
 * reverb (Dattorro) is configured in the reverb stage itself, not here. */
static void fc_push_params(looper_t* l, granular_t* g, microloop_t* m,
                           harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr) {
    double bpm = p->bpm > 0 ? p->bpm : 120.0;
    float bars = p->loop_length_bars; if (bars < 0.5f) bars = 0.5f; if (bars > 8) bars = 8;
    looper_set_loop_len(l, (int)(bars * FC_BEATS_PER_BAR * 60.0f / bpm * sr));
    looper_set_layer(l, p->loop_layer);   /* Event Horizon drains this toward 0.08 via the macro lerp */
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
    /* The chord itself (powf per voice) is rebuilt in fc_render_block only on a key/chord
     * change, not here — this runs on every param move (Flux, a Gravity ramp). */
    granular_set_mod_rate_hz(g, 0.05f * expf(p->mod_rate * 5.075f));   /* granular pitch-mod LFO rate */
}

/* Process ONE block of n (<= FC_BLK) frames. State persists in st across calls. */
static void fc_render_block(fc_state* st, looper_t* l, granular_t* g, microloop_t* m,
                            harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                            const float* in_l, const float* in_r, float* out_l, float* out_r, int n) {
    if (n > FC_BLK) n = FC_BLK;
    const float kLoopBedMakeup = 1.9f;   /* plugin processBlock value */
    const float scatter  = p->scatter;
    const float cleanG   = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmerG = 0.55f + 0.30f * scatter;   /* grain floor so the pitched (oct/5th)
                                                         grains blend as an in-key bed */
    const float driftFbGain = 0.22f * p->drift_amt * (1.0f - 0.78f * p->decay) * (1.0f - 0.50f * p->spectra);
    /* Plugin one-pole cutoffs at the host rate: a 5 Hz DC blocker and a 2500 Hz low-pass
     * on the drift-regen feedback. The regen LP must stay this bright — a sub-bass cutoff
     * makes the Flux wash spiral DOWN in pitch (feeds back only lows). */
    const float dcIC = 1.0f - expf(-6.2831853f *    5.0f / (float) sr);
    const float fbIC = 1.0f - expf(-6.2831853f * 2500.0f / (float) sr);

    /* Re-push coefficients only when params change, and — because the macro ramps
     * (Gravity/Flux) change a param every 2 ms block for ~2 s — throttle continuous
     * changes to every 4th block (~8 ms; inaudible, and each module smooths its own
     * target per-sample). Key/chord changes are discrete, so they push immediately. */
#ifdef AMB_PROFILE
    unsigned int _tp = time_us();
#endif
    {
        int keyChord = !st->have_pushed || p->key != st->last_pushed.key || p->chord != st->last_pushed.chord;
        int changed  = !st->have_pushed || memcmp(p, &st->last_pushed, sizeof(full_params)) != 0;
        if (changed && (keyChord || (st->push_ctr & 3u) == 0u)) {
            fc_push_params(l, g, m, h, b, d, p, sr);
            if (keyChord) {
                float f[HARMONY_MAX_VOICES]; int nv = fc_build_chord(p->key, p->chord, f);
                harmony_set_chord(h, f, nv);
            }
            st->last_pushed = *p;
            st->have_pushed = 1;
        }
        st->push_ctr++;
    }
#ifdef AMB_PROFILE
    g_stage_us[6] += time_us() - _tp;   /* push (incl. its re-fire cost during a ramp) */
    unsigned int _tl = time_us();
    #define STG(k) do { unsigned int _tn = time_us(); g_stage_us[k] += _tn - _tl; _tl = _tn; } while (0)
#else
    #define STG(k)
#endif

    /* Event Horizon (horizon 1 = full sustain, lower = drain). The drain is done cheaply
     * by the macro lerp pulling the loop + micro feedback down (so the buffers decay) plus
     * the wet-tail duck below — not by scanning the whole loop buffer each block, which is
     * too expensive on the RP2350. */
    const float horizonClear = p->horizon >= 1.0f ? 0.0f : 1.0f - p->horizon;   /* 0..1 */

    for (int i = 0; i < n; i++) {                        /* DC block */
        st->dcL += dcIC * (in_l[i] - st->dcL); st->dcR += dcIC * (in_r[i] - st->dcR);
        st->dryL[i] = in_l[i] - st->dcL; st->dryR[i] = in_r[i] - st->dcR;
    }
    for (int i = 0; i < n; i++) { st->srcL[i] = st->dryL[i]; st->srcR[i] = st->dryR[i]; }
    if (driftFbGain > 0.0f || st->driftFbCur > 1e-6f)   /* drift regeneration */
        for (int i = 0; i < n; i++) {
            st->driftFbCur += 0.0007f * (driftFbGain - st->driftFbCur);
            float fl = (i < st->revFbN) ? st->revFbL[i] : 0.f, fr = (i < st->revFbN) ? st->revFbR[i] : 0.f;
            st->fbL += fbIC * (fl - st->fbL); st->fbR += fbIC * (fr - st->fbR);
            st->srcL[i] += st->driftFbCur * fast_tanhf(st->fbL); st->srcR[i] += st->driftFbCur * fast_tanhf(st->fbR);
        }

    looper_process(l, st->srcL, st->srcR, st->loopL, st->loopR, n);
    for (int i = 0; i < n; i++) { st->loopL[i] *= kLoopBedMakeup; st->loopR[i] *= kLoopBedMakeup; }
    STG(0);   /* looper + drift regen */

    granular_process(g, st->loopL, st->loopR, st->granL, st->granR, n);
    for (int i = 0; i < n; i++) { st->layL[i] = cleanG * st->loopL[i] + shimmerG * st->granL[i];
                                  st->layR[i] = cleanG * st->loopR[i] + shimmerG * st->granR[i]; }
    STG(1);   /* granular */

    bloom_process(b, st->srcL, st->srcR, st->blL, st->blR, n);

    microloop_process(m, st->blL, st->blR, st->micL, st->micR, n);
    /* Satellite makeup (plugin processBlock): the microloop's own out-gain is
     * 0.25*sqrt(hold), far quieter than the loop bed. Invert toward a flat ~1.1 low/mid
     * level (clamped 1..14), taper to unity approaching freeze, then a -25% trim. */
    { float mh = p->micro_hold < 0.f ? 0.f : (p->micro_hold > 1.f ? 1.f : p->micro_hold);
      float og = 0.25f * sqrtf(mh > 1.0e-4f ? mh : 1.0e-4f);
      float mk = 1.1f / og; if (mk < 1.f) mk = 1.f; else if (mk > 14.f) mk = 14.f;
      float tp = (mh - 0.7f) * (1.0f / 0.3f); if (tp < 0.f) tp = 0.f; else if (tp > 1.f) tp = 1.f;
      mk += (1.0f - mk) * tp; mk *= 0.75f;
      for (int i = 0; i < n; i++) { st->micL[i] *= mk; st->micR[i] *= mk; } }
    /* Micro reverb send: full for short/mid delays, reduced in the held-pad zone (plugin).
     * rin = bloom + layered + microRevSend*micro; the wet bus below uses the full micro. */
    { float mp = (p->micro_hold - 0.80f) * (1.0f / 0.15f); if (mp < 0.f) mp = 0.f; else if (mp > 1.f) mp = 1.f;
      float microRevSend = 1.0f - 0.45f * mp;
      for (int i = 0; i < n; i++) { st->rinL[i] = st->blL[i] + st->layL[i] + microRevSend * st->micL[i];
                                    st->rinR[i] = st->blR[i] + st->layR[i] + microRevSend * st->micR[i]; } }
    STG(2);   /* bloom + microloop + makeup */

    { float t = (p->decay - 0.30f) * (1.0f / 0.70f); if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
      dattorro_set_decay(st->dat, t);                       /* Tail -> tail length (tracks the plugin) */
      dattorro_set_mod(st->dat, 0.25f + 0.75f * p->mod_depth); /* Flux + a floor so the tank always shimmers */
      dattorro_set_damp(st->dat, 0.22f); }                  /* bright, lush tail */
    dattorro_process(st->dat, st->rinL, st->rinR, st->wetL, st->wetR, n);
    STG(3);   /* reverb (Dattorro) */

    if (horizonClear > 0.001f) {                 /* Event Horizon ducks the reverb wash (1..0.15) */
        float wetTailGain = 1.0f - 0.85f * horizonClear;
        for (int i = 0; i < n; i++) { st->wetL[i] *= wetTailGain; st->wetR[i] *= wetTailGain; }
    }
    /* Drift regen taps the PRE-harmony wet (reverb wash only). The plugin taps post-harmony
     * so the chord recirculates, but the port's hotter Dattorro turns that into a tuned
     * runaway at the chord pitches (feedback that changes with the chord), so feed back only
     * the wash and let harmony sit on top. */
    for (int i = 0; i < n; i++) { st->revFbL[i] = st->wetL[i]; st->revFbR[i] = st->wetR[i]; } st->revFbN = n;
    harmony_process(h, st->wetL, st->wetR, st->wetL, st->wetR, n);          /* add the Spectra chord */
    for (int i = 0; i < n; i++) { st->wbL[i] = st->layL[i] + st->micL[i] + st->wetL[i];
                                  st->wbR[i] = st->layR[i] + st->micR[i] + st->wetR[i]; }
    STG(4);   /* harmony (Spectra) */
    drift_process(d, st->wbL, st->wbR, st->wbL, st->wbR, n);                /* wet-bus detune */

    /* Equal-power dry/wet crossfade (plugin): 50% keeps full loudness vs a linear blend's
     * -6 dB dip. Gains computed once per block — mix is already smoothed upstream, and
     * per-sample fast_cosf/sinf here would be over the core1 budget. */
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
        if (gravAmt > 0.001f) {
            gph += tremInc; if (gph > 6.2831853f) gph -= 6.2831853f;
            float trem = 1.0f - tremDepth * (0.5f - 0.5f * fast_cosf(gph));
            lv *= trem; rv *= trem;
        }
#ifdef FC_SOFT_CLIP
        lv = fast_tanhf(lv); rv = fast_tanhf(rv);   /* soft-clip the hot poly bus; plugin hard-clips */
#else
        lv = lv < -1 ? -1 : lv > 1 ? 1 : lv; rv = rv < -1 ? -1 : rv > 1 ? 1 : rv;
#endif
        out_l[i] = lv; out_r[i] = rv;
    }
    st->gravPhase = gph;
    STG(5);   /* drift + mix/output */
#ifdef AMB_PROFILE
    g_stage_n++;
#endif
    #undef STG
}

/* Harness convenience: render `total` frames in FC_BLK chunks (one fc_state). */
static void fc_render(looper_t* l, granular_t* g, microloop_t* m,
                      harmony_t* h, bloom_t* b, drift_t* d, const full_params* p, double sr,
                      const float* in_l, const float* in_r, float* out_l, float* out_r, int total) {
    static fc_state st;                       /* static: keep ~22 KB off the stack */
    fc_init(&st, p->mix);
    if (!st.dat) st.dat = dattorro_create(sr);
    for (int start = 0; start < total; start += FC_BLK) {
        int n = (FC_BLK < total - start) ? FC_BLK : (total - start);
        fc_render_block(&st, l, g, m, h, b, d, p, sr, in_l + start, in_r + start, out_l + start, out_r + start, n);
    }
}
#endif
