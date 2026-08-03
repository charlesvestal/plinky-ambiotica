/* New Phrase end-to-end, over the REAL chain.
 *
 * panel/newphrase_test.c pins the hold timer in isolation. This drives every module through
 * fc_render_block exactly as the panel does, which is the only way to see the things the unit
 * test cannot: what the looper's write law does to a freshly cleared buffer, and whether the
 * plate is still ringing after the clear.
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
   Returns the worst peak seen. 0.12, not 0.4: at 1.0 mix and 100% Layer the loop bed's makeup
   gains push a 0.4-amplitude tone past unity, into the harness's hard clip (FC_SOFT_CLIP is
   only defined in panel.cpp) - every peak below then reads 1.0000 whatever the looper does,
   and the refill ratio is 100% whether or not the bug is fixed. 0.12 keeps `built` in the
   low-0.6s, comfortably in the linear region; see the guard right after `built` below. */
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
    /* The case that exposes the write law: LOOP at maximum, where fb caps at 0.97 and the
       input term is 0.03. One bar at 120 bpm is 2 s, short enough to measure a single pass. */
    p.loop_layer = 1.0f;
    p.loop_length_bars = 1.0f; p.micro_bars = 0.25f;

    fc_init(&st, p.mix);
    st.dat = dattorro_create(SR);
    if (!st.dat) { fprintf(stderr, "dattorro create failed\n"); return 2; }

    int fail = 0;
    const int loop_len = SECS(2.0);    /* 1 bar at 120 bpm, 4 beats per bar */

    /* ---- establish a loop ---- */
    float built = run(SECS(12), 1);
    printf("after 12 s of playing:                 peak %.4f\n", built);
    if (built < 0.05f) { printf("  (nothing built up - test is meaningless)\n"); return 1; }

    /* The chain hard-clips at unity in the harness (FC_SOFT_CLIP is only defined in
       panel.cpp). If the built-up level reaches the clipper, every peak below reads 1.0000
       regardless of what the looper actually did, the refill ratio is always 100%, and this
       whole file proves nothing while printing PASS. Ask the question loudly rather than
       letting a silent clip turn the suite green. */
    if (built > 0.90f) {
        printf("\nFAIL: built to %.4f, at or near the clipper - the measurement is blind.\n"
               "      Lower the tone amplitude in run() until this is comfortably under 0.9.\n",
               built);
        return 1;
    }

    /* ---- clear, then keep playing at the same level ---- */
    looper_mark_clear(l);
    microloop_mark_clear(m);
    /* Advance exactly one loop pass and DISCARD it. drain_stale is `age > since_clear`, so the
       loop's own read is silent for samples 0..loop_len-1 after a clear and only comes back at
       sample loop_len. Measuring across that window sees granular and micro-loop residue and
       nothing of the looper at all, which is what an earlier version of this test did - it read
       identical numbers fixed and unfixed. */
    run(loop_len, 1);
    float refilled = run(SECS(0.25), 1);
    printf("one loop pass after the clear:         peak %.4f\n", refilled);

    /* The bug: buf = (1-fb)*in + fb*old with fb 0.97 means the input enters at 3% once `old`
       reads as silence, so a cleared loop takes ~33 passes to come back to level. For a
       gesture called "new phrase" that reads as broken. Full gain while there is nothing to
       mix with fixes it in exactly one pass. */
    const float WANT = 0.80f * built;
    if (refilled < WANT) {
        printf("\nFAIL: refill reached %.4f of %.4f (%.0f%%), want >= 80%%\n",
               refilled, built, (double)(100.f * refilled / built));
        fail = 1;
    } else {
        printf("\nPASS: refill reaches %.0f%% of the pre-clear level in one pass\n",
               (double)(100.f * refilled / built));
    }
    return fail;
}
