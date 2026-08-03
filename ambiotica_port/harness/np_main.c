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
    /* Measured against the signal's OWN slew rather than against its peak. Comparing to the
       peak would let a hard cut pass: the cut removes the bed but not the wet, so the step is
       smaller than the full output and a peak-based limit never fires. What a click actually
       is, is a jump far larger than anything the running signal produces, so that is what
       gets measured - baseline first, then the cut. */
    run(SECS(6), 1);
    float baseline = 0.f;
    g_prev_l = outL[BLK-1]; g_prev_r = outR[BLK-1];
    for (int n = 0; n < SECS(0.2); n += BLK) {
        for (int i = 0; i < BLK; i++) {
            float s = (float)(sin(phase) * 0.4); phase += 2.0 * M_PI * 220.0 / SR;
            inL[i] = inR[i] = s;
        }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = max_step(outL, outR, BLK); if (q > baseline) baseline = q;
    }
    looper_mark_clear(l); microloop_mark_clear(m);
    st.cutPending = 1;
    float step = 0.f;
    for (int n = 0; n < SECS(0.05); n += BLK) {
        for (int i = 0; i < BLK; i++) { inL[i] = inR[i] = 0.f; }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = max_step(outL, outR, BLK); if (q > step) step = q;
    }
    printf("largest jump across the cut:           %.4f (steady play was %.4f)\n", step, baseline);
    if (step > 3.0f * baseline) {
        printf("\nFAIL: the cut jumps %.1fx the signal's own slew - it will click\n",
               (double)(step / baseline));
        fail = 1;
    } else {
        printf("PASS: the cut is smoothed into a slope\n");
    }

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

    return fail;
}
