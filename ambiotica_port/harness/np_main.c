/* New Phrase end-to-end, over the REAL chain.
 *
 * panel/newphrase_test.c pins the hold timer in isolation. This drives every module through
 * fc_render_block exactly as the panel does, which is the only way to see the things a unit
 * test cannot: whether the plate is still ringing after a clear, and whether a long hold
 * actually collapses it.
 *
 * As in eh_main.c: ONE persistent fc_state, driven a block at a time. NOT fc_render, which
 * calls fc_init - a memset of the whole state including the dattorro pointer - on every call,
 * so looping over it per block silently rebuilds the chain and passes vacuously.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "full_chain.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POOL_BYTES (24u * 1024u * 1024u)
static unsigned char g_pool[POOL_BYTES];
static size_t g_used = 0;
static void* bump(size_t bytes) {
    size_t a = (g_used + 7u) & ~((size_t)7u);
    if (a + bytes > POOL_BYTES) { fprintf(stderr, "POOL OOM\n"); return NULL; }
    g_used = a + bytes; return g_pool + a;
}
void* amb_calloc(size_t n, size_t sz) { size_t b = n*sz; void* p = bump(b); if (p) memset(p,0,b); return p; }
void* amb_malloc(size_t sz)           { return bump(sz); }
void* amb_realloc(void* p, size_t sz) { (void)p; return bump(sz); }
void  amb_free(void* p)               { (void)p; }

#define SR      32000
#define BLK     FC_BLK
#define SECS(x) ((int)((x) * SR))

static float inL[BLK], inR[BLK], outL[BLK], outR[BLK];
static fc_state st;

static looper_t   *l; static granular_t *g; static microloop_t *m;
static harmony_t  *h; static bloom_t    *b; static drift_t     *d;
static full_params p;
static double phase = 0.0;

static float peak(const float *a, const float *c, int n) {
    float pk = 0.f;
    for (int i = 0; i < n; i++) {
        float lv = fabsf(a[i]), rv = fabsf(c[i]);
        if (lv > pk) pk = lv; if (rv > pk) pk = rv;
    }
    return pk;
}

/* Largest sample-to-sample jump in a block, carried across the block boundary. A hard cut
   shows up here as a step the size of the signal that was cut; a declicked one does not. */
static float g_prev_l = 0.f, g_prev_r = 0.f;
static float max_step(const float *a, const float *c, int n) {
    float mx = 0.f;
    for (int i = 0; i < n; i++) {
        float dl = fabsf(a[i] - g_prev_l), dr = fabsf(c[i] - g_prev_r);
        if (dl > mx) mx = dl; if (dr > mx) mx = dr;
        g_prev_l = a[i]; g_prev_r = c[i];
    }
    return mx;
}

/* Render `samples` frames. tone != 0 plays a 220 Hz sine in; otherwise the input is silent.
   Returns the worst peak seen. 0.12, not something louder: at 1.0 mix and 100% Layer the loop
   bed's makeup gains push a hotter tone past unity, into the harness's hard clip (FC_SOFT_CLIP
   is only defined in panel.cpp) - every peak above that reads 1.0000 regardless of what the
   chain actually did. See the guard on `built` below. */
static float run(int samples, int tone) {
    float worst = 0.f;
    for (int n = 0; n < samples; n += BLK) {
        for (int i = 0; i < BLK; i++) {
            float s = tone ? (float)(sin(phase) * 0.12) : 0.f;
            phase += 2.0 * M_PI * 220.0 / SR;
            inL[i] = inR[i] = s;
        }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = peak(outL, outR, BLK); if (q > worst) worst = q;
    }
    return worst;
}

/* Same as run(), but with the tone amplitude as a parameter instead of run()'s fixed 0.12 -
 * needed because the diagnostic below runs micro_hold around 0.6, where full_chain.h's own
 * Satellite makeup gain (mk, up to ~4.3x at micro_hold=0.6: og=0.25*sqrt(0.6), mk=1.1/og*0.75)
 * pushes 0.12 well past the clipper on its own. Never touch the clipping guard - lower the
 * amplitude here instead, per the task.
 *
 * Also tracks the peak of st.micL/st.micR - the micro-loop's OWN output, before it is summed
 * with the looper/granular bed and the reverb wet into wbL/wbR and mixed to out_l/out_r. Reading
 * it is legitimate without touching dsp/ or full_chain.h: fc_state is a plain struct, not
 * opaque, and micL/micR are already fields on it - this just looks at state that already exists,
 * the same way the AMB_PROFILE block inside full_chain.h itself does. If mic_peak_out is
 * non-NULL, *mic_peak_out receives the worst |micL|/|micR| seen. Used so the chain-output peak
 * (contaminated by the plate's own tail - see the block comment above) is not the only number
 * this diagnostic can offer. */
static float run_amp(int samples, float amp, float *mic_peak_out) {
    float worst = 0.f, mic_worst = 0.f;
    for (int n = 0; n < samples; n += BLK) {
        for (int i = 0; i < BLK; i++) {
            float s = (float)(sin(phase) * amp);
            phase += 2.0 * M_PI * 220.0 / SR;
            inL[i] = inR[i] = s;
        }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = peak(outL, outR, BLK); if (q > worst) worst = q;
        float qm = peak(st.micL, st.micR, BLK); if (qm > mic_worst) mic_worst = qm;
    }
    if (mic_peak_out) *mic_peak_out = mic_worst;
    return worst;
}

/* One 16-phase declick sweep. Rebuilds the bed, cuts it, and repeats 16 times, each time after
   a different amount of extra tone so the cut lands at a different point in the 220 Hz cycle -
   see the call site for why a single cut can't tell this apart. `declick` selects whether the
   cut sets st.cutPending: when it does, the residual fires (see full_chain.h's wb sum); when it
   doesn't, looper_mark_clear/microloop_mark_clear still run and the bed still drops, but cutL/
   cutR are never seeded, so the drop is left raw. Returns the worst step seen across the 16. */
static float declick_sweep(int declick) {
    float worst = 0.f;
    for (int k = 0; k < 16; k++) {
        run(SECS(6), 1);                              /* rebuild the bed after the previous cut */
        for (int extra = 0; extra <= k; extra++) {     /* land this cut at a different phase;
                                                            always at least one block so g_prev
                                                            is a fresh pre-cut sample below, not
                                                            a stale one from the last sweep */
            for (int i = 0; i < BLK; i++) {
                float s = (float)(sin(phase) * 0.4); phase += 2.0 * M_PI * 220.0 / SR;
                inL[i] = inR[i] = s;
            }
            fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        }
        g_prev_l = outL[BLK-1]; g_prev_r = outR[BLK-1];   /* last pre-cut sample */
        looper_mark_clear(l); microloop_mark_clear(m);
        if (declick) st.cutPending = 1;
        for (int n = 0; n < SECS(0.05); n += BLK) {
            for (int i = 0; i < BLK; i++) { inL[i] = inR[i] = 0.f; }
            fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
            float q = max_step(outL, outR, BLK); if (q > worst) worst = q;
        }
    }
    return worst;
}

/* ============================================================================================
 * Does a single microloop_mark_clear actually silence the micro-loop for as long as a human
 * calls it "cleared", or does drift regeneration write the buffer full again before the read
 * tap catches up? Bug report: on hardware, a single x+REC TAP clears the looper audibly but
 * the micro-loop "does not seem to clear". Root cause: mark_clear silences READS older than
 * the clear but does nothing to stop new WRITES, and full_chain.h's drift regen keeps feeding
 * the reverb wash into srcL/srcR - which reaches the micro-loop - whether or not the loop it
 * is supposedly regenerating is empty. Fix: driftFbGain is gated to 0 while looper_is_empty(l)
 * (full_chain.h), so a cleared loop refills from what you play, not from the tail of what you
 * cleared.
 *
 * Isolation choice - decay: lowered from the suite's 0.6 to 0.35 (spectra stays 0, as
 * elsewhere) so the plate's own tail is short and does not dominate 3 s of listening, but NOT
 * all the way to 0. `full_chain.h` uses p->decay in two other places besides driftFbGain's own
 * factor:
 *   - t = (decay - 0.30) / 0.70, clamped 0..1, fed to dattorro_set_decay - below decay=0.30
 *     this is exactly 0: the tank has NO internal sustain, each block just diffuses whatever
 *     enters once and stops holding it.
 *   - dryToRev = decay - the send of blL/blR (bloom's near-transparent-at-bloom=0 copy of
 *     srcL/srcR) into the reverb: rinL = dryToRev*blL + layL + microRevSend*micL.
 * At decay=0 BOTH go to zero together: the tank cannot hold anything AND nothing but layL/micL
 * (themselves near-silent right after a clear) feeds it, so wetL/R collapses near-silent fast,
 * driftFbCur has nothing to tap, and the regen path under test would go quiet for a reason that
 * has nothing to do with the micro-loop. (driftFbGain's OWN (1 - 0.78*decay) factor does NOT
 * need decay>0 - it is actually LARGEST at decay=0 - so that term alone would not have flagged
 * this.) 0.35 sits just above the t=0 floor: a short but real tank tail (t=0.071) and a present
 * dry send (dryToRev=0.35), while driftFbGain's decay factor is 0.727 of its max - comparable
 * to, if not higher than, the suite's default (0.532 at decay=0.6).
 */
#define NP_DIAG_DECAY   0.35f
#define NP_DIAG_WINDOWS 30
#define NP_DIAG_WINSEC  0.1
/* Tone amplitude for the build-up phase only. Lowered from run()'s 0.12 because micro_hold=0.6
 * (vs the suite's 0) adds a large Satellite makeup gain (see run_amp's comment) - found by
 * measuring `built`, per the clipping-guard rule: lower the amplitude, never the guard. */
#define NP_DIAG_TONE_AMP 0.035f

/* Scenario A - TAP, drift regen ON (drift_amt=0.3): the bug case. WITHOUT the driftFbGain gate
 * (temporarily reverted to prove this number, then restored - see the commit message), worst
 * isolated-mic peak measured 0.0163 at t=0.5-0.6s and was STILL 0.0011 at t=2.9-3.0s: a
 * continuously self-refilling micro-loop, audible for the whole 3 s window.
 *
 * WITH the gate but the ORIGINAL ~45 ms driftFbCur one-pole (the coefficient sized for ordinary
 * drift_amt/decay/spectra moves, not for a hard gate), worst isolated-mic peak measured 0.0118:
 * better, but still 17x the no-regen floor (scenario B: 0.0007). driftFbGain's TARGET snaps to
 * 0 the instant looper_is_empty(l) goes true, but driftFbCur takes ~45 ms to follow, and
 * whatever it feeds into srcL/srcR during that window lands in the micro-loop's own dead window
 * (before its own loop_len makes reads live again) and gets sustained by the micro-loop's OWN
 * feedback (micro_hold=0.6 here) for seconds afterward.
 *
 * Fix: full_chain.h now uses a separate ~5 ms coefficient (driftFbFastIC) ONLY while the loop
 * is empty - ordinary drift_amt/decay/spectra changes, and the loop refilling and un-gating,
 * still ride the original 45 ms fade and keep their existing feel. With that, worst isolated-mic
 * peak measured 0.0083 - a real drop from 0.0118, but nowhere near the ~9x (45/5) a naive
 * time-constant ratio would suggest. Traced with a temporary probe (not shipped): the WORST
 * sample is set by driftFbCur's value AT THE INSTANT of the clear, which is identical whether
 * the coefficient afterward is fast or slow - both curves start from the same pre-clear
 * steady-state value and have not yet diverged. The fast coefficient only speeds how quickly
 * LATER samples fall away, shrinking the contaminated window's tail, not its peak. Getting
 * materially closer to the 0.0007 floor would mean touching driftFbCur's value at the gate
 * transition itself (a discontinuity, the thing the one-pole exists to avoid) or the
 * micro-loop's own capture feedback - both out of scope here.
 *
 * The same probe measured the fast coefficient's own worst per-sample step in driftFbCur:
 * 0.0003 (driftFbFastIC * driftFbCur's typical pre-clear level here, ~0.006 * 0.048) - about
 * 130-200x smaller than this file's own declick_sweep numbers a few dozen lines up (raw jump
 * 0.0616, declicked to 0.0398), which is this project's own working definition of "small enough
 * not to click". Not audible as a step.
 *
 * 0.011 sits with real margin below the gate-removed number (0.0163) and above the fixed one
 * (0.0083) - it catches the gate being removed, the coefficient being reverted to the slow one,
 * or the wrong buffer being checked, without chasing the micro-loop's own decaying tail (whose
 * mechanism is understood, see above) down to the noise floor. */
#define NP_DIAG_A_LIMIT   0.011f
/* Scenario B - TAP, drift regen OFF (drift_amt=0.0): the control. driftFbGain is 0 throughout
 * regardless of the gate, so this is what "clear" looks like with nothing to refill from -
 * measured 0.0007. 0.002 gives headroom for float noise without hiding a genuinely broken
 * control (which would silently invalidate the margin NP_DIAG_A_LIMIT above is built on). */
#define NP_DIAG_B_LIMIT   0.002f
/* Scenario C - HOLD, drift regen ON (drift_amt=0.3): an invariant, not a regression case. HOLD
 * re-stamps both mark_clears every block (as the real x+REC hold does), so the micro-loop's own
 * read never stops being stale for the whole 3 s - true independent of this fix. Measured
 * 0.0000; pinned so a future change to the hold path cannot silently break it. */
#define NP_DIAG_C_LIMIT   0.001f

static float np_diag_out_peaks[NP_DIAG_WINDOWS];   /* full chain output - loop+gran+mic+plate+dry */
static float np_diag_mic_peaks[NP_DIAG_WINDOWS];   /* st.micL/micR alone - the micro-loop's own
                                                        output, isolated from the plate's tail */

/* Render NP_DIAG_WINDOWS windows of NP_DIAG_WINSEC seconds each with silent input, storing the
 * peak of each window in np_diag_out_peaks/np_diag_mic_peaks (caller prints/asserts). hold_clear:
 * re-stamp BOTH mark_clears every block, as the real x+REC hold does; 0 = the clear already
 * happened once, just listen - the TAP case. */
static void np_diag_windows(int hold_clear) {
    int wsamp = (int)(NP_DIAG_WINSEC * SR + 0.5);
    for (int w = 0; w < NP_DIAG_WINDOWS; w++) {
        float worst_out = 0.f, worst_mic = 0.f;
        for (int n = 0; n < wsamp; n += BLK) {
            int take = (BLK < wsamp - n) ? BLK : (wsamp - n);
            if (hold_clear) { looper_mark_clear(l); microloop_mark_clear(m); }
            for (int i = 0; i < take; i++) { inL[i] = inR[i] = 0.f; }
            fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, take);
            float qo = peak(outL, outR, take); if (qo > worst_out) worst_out = qo;
            float qm = peak(st.micL, st.micR, take); if (qm > worst_mic) worst_mic = qm;
        }
        np_diag_out_peaks[w] = worst_out;
        np_diag_mic_peaks[w] = worst_mic;
    }
}

/* One full scenario: reset every module to silence (RT-safe resets, no realloc - see
 * amb_calloc/amb_free in this file: free() is a no-op, so re-creating modules per scenario
 * would burn through the 24 MB bump pool for no reason), set params, build 8 s of material,
 * clear once (or hold it every block for the whole measurement), then measure. Prints a
 * decimated window table (every 5th, plus the last) so a human still sees the decay shape
 * without 30 rows of noise. *worst_out_out/*worst_mic_out receive the worst peaks seen: the
 * caller asserts on them. Returns 1 if the build clipped or never built up - measurement not
 * trustworthy, caller must treat this as a failure rather than assert on meaningless numbers. */
static int np_diag_scenario(const char *label, float drift_amt, int hold_clear,
                             float *worst_out_out, float *worst_mic_out) {
    dattorro_t *dat = st.dat;              /* fc_init below memsets st, including this pointer -
                                               save it and restore it rather than leaking a new
                                               dattorro_create() into the bump pool every call */
    looper_reset(l); granular_reset(g); microloop_reset(m);
    harmony_reset(h); bloom_reset(b);      drift_reset(d);
    fc_init(&st, 1.0f);
    st.dat = dat;
    dattorro_reset(st.dat);
    phase = 0.0;
    g_prev_l = g_prev_r = 0.f;

    memset(&p, 0, sizeof p);
    p.mix = 1.0f; p.grain_size = 0.4f; p.scatter = 0.f;
    p.micro_hold = 0.6f; p.decay = NP_DIAG_DECAY; p.mod_depth = 0.3f; p.mod_rate = 0.4f;
    p.bloom = 0.f; p.drift_amt = drift_amt; p.spectra = 0.f; p.ring = 0.f;
    p.bpm = 120.f; p.key = 0; p.chord = 0;
    p.gravity = 0.f; p.horizon = 1.f; p.dilate = 0.f;
    p.loop_layer = 1.0f;
    p.loop_length_bars = 1.0f; p.micro_bars = 0.125f;   /* small, per the task: shortest window */

    float built_mic = 0.f;
    float built = run_amp(SECS(8), NP_DIAG_TONE_AMP, &built_mic);
    printf("\n[%s]\n  after 8 s of playing:  chain peak %.4f   isolated micL/R peak %.4f\n",
           label, (double)built, (double)built_mic);
    if (built > 0.90f) {
        printf("  BLOCKED: built to %.4f, at or near the clipper - not trustworthy.\n", (double)built);
        return 1;
    }
    if (built < 0.05f || built_mic < 0.01f) {
        printf("  BLOCKED: nothing built up (chain %.4f, mic %.4f) - measurement would be "
               "meaningless.\n", (double)built, (double)built_mic);
        return 1;
    }

    looper_mark_clear(l); microloop_mark_clear(m);   /* the clear itself: always at least once,
                                                          whether this is a tap or the first
                                                          block of a hold */
    np_diag_windows(hold_clear);

    printf("           chain out (loop+gran+mic+plate+dry)   isolated micL/R (mic stage alone)\n");
    float worst_out = 0.f, worst_mic = 0.f; int worst_out_w = 0, worst_mic_w = 0;
    for (int w = 0; w < NP_DIAG_WINDOWS; w++) {
        if (w % 5 == 0 || w == NP_DIAG_WINDOWS - 1)
            printf("  t=%4.1f-%4.1fs  %.4f                              %.4f\n",
                   (double)(w * NP_DIAG_WINSEC), (double)((w + 1) * NP_DIAG_WINSEC),
                   (double)np_diag_out_peaks[w], (double)np_diag_mic_peaks[w]);
        if (np_diag_out_peaks[w] > worst_out) { worst_out = np_diag_out_peaks[w]; worst_out_w = w; }
        if (np_diag_mic_peaks[w] > worst_mic) { worst_mic = np_diag_mic_peaks[w]; worst_mic_w = w; }
    }
    printf("  worst chain-out peak %.4f at t=%.1f-%.1fs\n", (double)worst_out,
           (double)(worst_out_w * NP_DIAG_WINSEC), (double)((worst_out_w + 1) * NP_DIAG_WINSEC));
    printf("  worst isolated-mic peak %.4f at t=%.1f-%.1fs\n", (double)worst_mic,
           (double)(worst_mic_w * NP_DIAG_WINSEC), (double)((worst_mic_w + 1) * NP_DIAG_WINSEC));
    if (worst_out_out) *worst_out_out = worst_out;
    if (worst_mic_out) *worst_mic_out = worst_mic;
    return 0;
}

int main(void) {
    const int loopcap = 32 * SR;
    l = looper_create(loopcap, SR); g = granular_create(SR); m = microloop_create(SR);
    h = harmony_create(SR);         b = bloom_create(SR);    d = drift_create(SR);
    if (!l || !g || !m || !h || !b || !d) { fprintf(stderr, "create failed\n"); return 2; }

    memset(&p, 0, sizeof p);
    p.mix = 1.0f; p.grain_size = 0.4f; p.scatter = 0.f;
    p.micro_hold = 0.f; p.decay = 0.6f; p.mod_depth = 0.3f; p.mod_rate = 0.4f;
    p.bloom = 0.f; p.drift_amt = 0.f; p.spectra = 0.f; p.ring = 0.f;
    p.bpm = 120.f; p.key = 0; p.chord = 0;
    p.gravity = 0.f; p.horizon = 1.f; p.dilate = 0.f;
    /* One bar at 120 bpm is 2 s, a convenient loop pass to reason about. */
    p.loop_layer = 1.0f;
    p.loop_length_bars = 1.0f; p.micro_bars = 0.25f;

    fc_init(&st, p.mix);
    st.dat = dattorro_create(SR);
    if (!st.dat) { fprintf(stderr, "dattorro create failed\n"); return 2; }

    int fail = 0;

    /* ---- establish a loop ---- */
    float built = run(SECS(12), 1);
    printf("after 12 s of playing:                 peak %.4f\n", built);
    if (built < 0.05f) { printf("  (nothing built up - test is meaningless)\n"); return 1; }

    /* The chain hard-clips at unity in the harness (FC_SOFT_CLIP is only defined in
       panel.cpp). If the built-up level reaches the clipper, every peak below reads 1.0000
       regardless of what the chain actually did, any ratio-based assertion reads 100%
       regardless of the code under test, and this whole file proves nothing while printing
       PASS. Ask the question loudly rather than letting a silent clip turn the suite green. */
    if (built > 0.90f) {
        printf("\nFAIL: built to %.4f, at or near the clipper - the measurement is blind.\n"
               "      Lower the tone amplitude in run() until this is comfortably under 0.9.\n",
               built);
        return 1;
    }

    /* NEW PHRASE REFILL CLAIM: PARKED, not tested here.
     *
     * This file used to clear the loop and assert it refilled to >= 80% of `built` within one
     * loop pass, backed by a looper.c write-law fix (take input at full gain while the read is
     * provably stale). The fix works - verified in isolation down to the exact boundary
     * sample - and getting a test that could actually see that took two more fixes: seeding a
     * fresh ring as already-recorded (a freshly-created looper is otherwise bit-identical to a
     * just-cleared one, so the fix fired at power-on too) and measuring one loop pass further
     * out (the original window ended exactly one sample before the read ever stops being
     * stale, so it read identical numbers fixed and unfixed). Both of those are real, and
     * proven.
     *
     * What isn't proven: with everything applied, eh_main.c's post-release remnant rose from
     * 0.0078 to 0.0237 - the same "stale" cursor that scopes the fix is also read by Dilate's
     * reverse heads, and gating the one other known reader (drift regeneration) barely moved
     * the number. Shipping a hardware behaviour change nobody can fully explain was judged
     * worse than the slow bloom-in at LOOP 100%, which is what the instrument already does. So
     * the fix, the seed, the regen gate, and the corrected measurement (advance one loop pass
     * and discard it, then measure) all live on `refill-fix-investigation` instead of here.
     * See docs/superpowers/notes/2026-08-03-looper-refill-investigation.md.
     */

    /* ---- the tail must survive the clear ---- */
    /* This is the feature. It is also the assertion that fails the day someone adds a plate
       reset for tidiness, which is why it is worth its own check rather than an eyeball. */
    run(SECS(4), 1);
    looper_mark_clear(l); microloop_mark_clear(m);
    st.tailKill = 0.f;
    float tail = run(SECS(0.5), 0);      /* input silent: anything heard is the plate */
    printf("half a second after a clear, no input: peak %.4f\n", tail);
    if (tail < 0.02f) {
        printf("\nFAIL: the tail died with the loop (%.4f) - the plate must ring out\n", tail);
        fail = 1;
    } else {
        printf("PASS: the plate is still ringing after the clear\n");
    }

    /* ---- the edge must be a slope, not a step ---- */
    /* LOOP 100% (loop_layer left at 1.0 above) caps feedback at 0.97, so the bed only reaches
       1 - 0.97^N of input level after N loop passes - under 10% after the three passes SECS(6)
       buys here. With mix at 1.0 (fully wet), micro_hold at 0 (micL negligible) and spectra at
       0 (harmony off), that means a cut at LOOP 100% removes almost nothing: there is no click
       to hide because there is nothing there. That is the slow-refill behaviour parked in
       Task 2, showing up here as a test artifact rather than a real absence of clicking. Lower
       Layer just for this check so the bed actually builds to something worth cutting - fb =
       0.25 converges to input level within about one pass instead of thirty-three - then
       restore LOOP 100% afterward so the long-hold check below keeps testing the case it
       already proved.
       No absolute bound on the step is meaningful, for two reasons found the hard way. First,
       the step is wg*(layL+micL) sampled at whatever instant the cut happens to land on, so it
       runs from nothing at a zero crossing to the full bed amplitude at a peak - one arbitrary
       cut phase tells you about that phase, not the worst case a declick exists for. Second, a
       fixed multiple of the ambient slew (an earlier version of this check used 3x) still does
       not gate on the residual even once the worst phase is found: raising the bed scales the
       step AND the ambient slew together, so their ratio barely moves whether cutL/cutR do
       anything or not.
       What actually matters is that the residual measurably shrinks the worst case a sweep can
       find, so that is the comparison this check makes directly: declick_sweep runs 16 cuts at
       16 different phases of the bed's cycle, once with the residual engaged (cutPending set on
       every cut - "declicked") and once with it left inert (the clear still happens, cutPending
       never set - "raw"), and this asserts the declicked worst case is meaningfully smaller
       than the raw one. Delete the residual and both calls run the same code path, the ratio
       goes to 1.0, and this fails.

       The threshold has moved twice. Originally measured raw 0.0652, declicked 0.0449, ratio
       0.69 against a 0.85 line. Seeding cutL/cutR straight from lastBedL/lastBedR (the residual
       as a raw value) measured raw 0.0616, declicked 0.0398, ratio 0.646 - no real change,
       because that seed was over-compensating: the bed does not actually reach zero at the cut
       (granular grains already in flight, the micro-loop's output one-pole decaying rather than
       dropping - see full_chain.h's wb-loop comment), so adding the FULL last bed value on top
       of what already survived just replaced a downward step with an upward one of about the
       same size. Seeding the residual as the GAP - lastBedL/R minus what the bed's first sample
       of the new block actually is - fixed that, and only then did the ratio move: raw 0.0616
       (unaffected, the raw path never sets cutPending), declicked 0.0309, ratio 0.502. The 0.65
       line leaves real margin above that without inheriting the old 0.85's slack. */
    p.loop_layer = 0.5f;
    float worst_declicked = declick_sweep(1);
    float worst_raw = declick_sweep(0);
    printf("worst jump across 16 cuts: declicked %.4f, raw %.4f (ratio %.3f)\n",
           worst_declicked, worst_raw, (double)(worst_declicked / worst_raw));
    if (worst_declicked >= 0.65f * worst_raw) {
        printf("\nFAIL: declicked (%.4f) is not meaningfully smaller than raw (%.4f) - the "
               "residual is not doing anything\n", worst_declicked, worst_raw);
        fail = 1;
    } else {
        printf("PASS: the residual cuts the worst-case jump to %.0f%% of the raw cut\n",
               (double)(100.0f * worst_declicked / worst_raw));
    }
    p.loop_layer = 1.0f;   /* restore LOOP 100% for the checks below */

    /* ---- a long hold collapses the plate ---- */
    /* This is tail_kill in isolation, so the loop has to be cleared here even though
       tail_kill itself never touches the loop - on the real pad the hold clears the loop and
       ramps tail_kill together (see Task 4's on_dsp), and with loop_layer at 1.0 and no leak
       an UNcleared loop is a genuinely persistent, non-decaying signal that tail_kill was
       never designed to silence; without the clear this assertion would fail regardless of
       what the DSP change under test does.
       The clear needs its own settling time too: reading age == loop_len is a single step,
       not a ramp (see looper.c), so for one full loop_len of samples after the clear every
       read at that age is stale and silent, and only past that point does the position hold
       whatever was actually written during the clear - here, more silence. A peak taken over
       the raw hold would be dominated by that flush plus the last diffusion pass still
       finishing in the tank, neither of which means the collapse failed. "After holding
       tail_kill for 2 s" describes the state at the END of the hold, so the first second is
       left unmeasured on purpose and only the second is checked - confirmed against a run
       with tail_kill left at 0 (same clear, same settle): that reads 0.0246, comfortably
       above the 0.01 line, so the margin below is tail_kill's decay collapse, not the clear
       or the passage of time on their own. */
    run(SECS(6), 1);
    looper_mark_clear(l); microloop_mark_clear(m);
    st.tailKill = 1.f;
    run(SECS(1.0), 0);
    float collapsed = run(SECS(1.0), 0);
    printf("2 s of full tail_kill, no input:       peak %.4f\n", collapsed);
    if (collapsed > 0.01f) {
        printf("\nFAIL: the plate is still sounding at %.4f after a full collapse\n", collapsed);
        fail = 1;
    } else {
        printf("PASS: a long hold empties the plate\n");
    }
    st.tailKill = 0.f;

    /* ---- a single micro-loop clear must actually stay silent ---- */
    printf("\n================ micro-loop clear vs drift regen ================\n");
    int diag_blocked = 0;
    float worst_a = 0.f, worst_b = 0.f, worst_c = 0.f;
    diag_blocked |= np_diag_scenario("A: TAP,  drift regen ON  (drift_amt=0.3)", 0.3f, 0, NULL, &worst_a);
    diag_blocked |= np_diag_scenario("B: TAP,  drift regen OFF (drift_amt=0.0)", 0.0f, 0, NULL, &worst_b);
    diag_blocked |= np_diag_scenario("C: HOLD, drift regen ON  (drift_amt=0.3)", 0.3f, 1, NULL, &worst_c);
    if (diag_blocked) {
        printf("\nFAIL: at least one scenario above could not get a clean measurement (see "
               "BLOCKED lines) - cannot assert on numbers that do not mean anything.\n");
        fail = 1;
    } else {
        /* Scenario A is the bug: see NP_DIAG_A_LIMIT above for the measured before/after and
           why the limit sits where it does. */
        if (worst_a > NP_DIAG_A_LIMIT) {
            printf("\nFAIL: A's worst isolated-mic peak %.4f exceeds %.4f - the cleared "
                   "micro-loop is refilling from drift regen\n", (double)worst_a, (double)NP_DIAG_A_LIMIT);
            fail = 1;
        } else {
            printf("\nPASS A: worst isolated-mic peak %.4f stays below %.4f - the clear holds\n",
                   (double)worst_a, (double)NP_DIAG_A_LIMIT);
        }
        if (worst_b > NP_DIAG_B_LIMIT) {
            printf("FAIL: B (the control, drift_amt=0) reads %.4f, above %.4f - the control "
                   "itself is not clean, so A's margin above is not trustworthy\n",
                   (double)worst_b, (double)NP_DIAG_B_LIMIT);
            fail = 1;
        } else {
            printf("PASS B: control stays below %.4f\n", (double)NP_DIAG_B_LIMIT);
        }
        if (worst_c > NP_DIAG_C_LIMIT) {
            printf("FAIL: C (HOLD) reads %.4f, above %.4f - a continuously re-stamped clear "
                   "should never let the micro-loop's read go live\n",
                   (double)worst_c, (double)NP_DIAG_C_LIMIT);
            fail = 1;
        } else {
            printf("PASS C: HOLD invariant stays below %.4f\n", (double)NP_DIAG_C_LIMIT);
        }
    }
    printf("===============================================================================\n");

    return fail;
}
