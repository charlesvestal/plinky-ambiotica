/* Event Horizon end-to-end: drive the REAL chain and check the loop actually goes away.
 *
 * The unit test in panel/drain_test.c models the ring in isolation. This drives every module
 * through fc_render exactly as the panel does, so it also covers the taps the model does not
 * know about - the crossfade tap, the Dilate reverse heads, the micro-loop grain cloud - and
 * the wiring in fc_push_params. It is the check that would have caught this on the desktop
 * instead of on the device.
 *
 * Phases: play into the chain, drop Horizon to 0 for a short hold (deliberately shorter than
 * a full sweep pass), release it back to full with NO further input, and then listen. Silence
 * after the release is the whole point of the feature.
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

/* One persistent chain state, driven a block at a time the way the panel does. NOT fc_render:
 * that calls fc_init (a memset of the whole state, dattorro pointer included) on every call,
 * so looping over it per block would rebuild the plate and reset the chain each time. */
static fc_state st;

/* Peak of one rendered block. */
static float peak(const float *a, const float *b, int n) {
    float p = 0.f;
    for (int i = 0; i < n; i++) {
        float l = fabsf(a[i]), r = fabsf(b[i]);
        if (l > p) p = l; if (r > p) p = r;
    }
    return p;
}

int main(void) {
    const int loopcap = 32 * SR;
    looper_t   *l = looper_create(loopcap, SR);
    granular_t *g = granular_create(SR);
    microloop_t*m = microloop_create(SR);
    harmony_t  *h = harmony_create(SR);
    bloom_t    *b = bloom_create(SR);
    drift_t    *d = drift_create(SR);
    if (!l || !g || !m || !h || !b || !d) { fprintf(stderr, "create failed\n"); return 2; }

    full_params p; memset(&p, 0, sizeof p);
    p.mix = 0.7f; p.loop_layer = 0.9f; p.grain_size = 0.4f; p.scatter = 0.5f;
    p.micro_hold = 0.6f; p.decay = 0.6f; p.mod_depth = 0.3f; p.mod_rate = 0.4f;
    p.bloom = 0.5f; p.drift_amt = 0.3f; p.spectra = 0.6f; p.ring = 0.5f;
    p.bpm = 120.f; p.key = 0; p.chord = 0;
    p.gravity = 0.f; p.horizon = 1.f;
    /* A LONG loop on purpose: 8 bars is far more than a brief hold can sweep, which is the
       case the sweep alone could never cover. Dilate on, so the reverse heads are live too. */
    p.loop_length_bars = 8.0f; p.micro_bars = 2.0f; p.dilate = 1.0f;

    fc_init(&st, p.mix);
    st.dat = dattorro_create(SR);
    if (!st.dat) { fprintf(stderr, "dattorro create failed\n"); return 2; }

    double phase = 0.0;
    int fail = 0;

    /* ---- phase 1: play 10 s of tone into the chain, Horizon wide open ---- */
    float built = 0.f;
    for (int n = 0; n < SECS(10); n += BLK) {
        for (int i = 0; i < BLK; i++) {
            float s = (float)(sin(phase) * 0.4); phase += 2.0 * M_PI * 220.0 / SR;
            inL[i] = inR[i] = s;
        }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = peak(outL, outR, BLK); if (q > built) built = q;
    }
    printf("after 10 s of playing:            peak %.4f\n", built);
    if (built < 0.05f) { printf("  (nothing built up - test is meaningless)\n"); fail = 1; }

    /* ---- phase 2: Horizon to 0 for 2 s, input silent. Shorter than a full sweep pass. ---- */
    memset(inL, 0, sizeof inL); memset(inR, 0, sizeof inR);
    p.horizon = 0.f;
    float during = 0.f;
    for (int n = 0; n < SECS(2); n += BLK) {
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = peak(outL, outR, BLK); if (q > during) during = q;
    }
    printf("holding Horizon at 0 for 2 s:     peak %.4f\n", during);

    /* ---- phase 3: release to full. NOTHING is played. Anything heard is a remnant. ---- */
    p.horizon = 1.f;
    float worst = 0.f; double worst_at = 0.0;
    for (int n = 0; n < SECS(20); n += BLK) {
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = peak(outL, outR, BLK);
        if (q > worst) { worst = q; worst_at = (double)n / SR; }
    }
    printf("20 s after release, no input:     peak %.4f (at +%.2f s)\n", worst, worst_at);

    /* The bug: the loop came back seconds after the slider was released. Reverb and Spectra
       tails are allowed to ring down, so this is not asking for a mathematical zero - but a
       returning LOOP is loud, roughly the level it was at before the drain. */
    const float LIMIT = 0.02f;
    if (worst > LIMIT) {
        printf("\nFAIL: %.4f exceeds %.4f - the loop came back %.2f s after release\n",
               worst, LIMIT, worst_at);
        fail = 1;
    } else {
        printf("\nPASS: stays below %.4f for 20 s after release - the clear holds\n", LIMIT);
    }
    return fail;
}
