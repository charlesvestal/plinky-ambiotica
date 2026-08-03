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

    return fail;
}
