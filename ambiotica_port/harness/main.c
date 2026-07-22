/* Ambiotica-on-Plinky: desktop port harness.
 *
 * Goal: validate that the vendored Ambiotica DSP runs with ZERO heap in the
 * process path, at Plinky's 32 kHz, using a fixed memory arena — and print a
 * per-module memory map we can drop onto Plinky's SRAM / PSRAM / flash split.
 *
 * The DSP .c files are compiled with calloc/malloc/free/realloc redirected to
 * the instrumented bump allocator below (see build.sh). This file is NOT
 * redirected: it defines the allocator and uses the real libc for its own I/O.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "ambiotica_chain.h"   /* looper/granular/microloop/reverb + ambt_params + ambt_render_chain */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef LOOPER_I16
#define VARIANT "int16 loop bed"
#define WAVNAME "out_i16.wav"
#else
#define VARIANT "float loop bed"
#define WAVNAME "out_float.wav"
#endif

/* ------------- instrumented bump allocator (models a fixed arena) ---------- */
#define POOL_BYTES (24u * 1024u * 1024u)
static unsigned char g_pool[POOL_BYTES];
static size_t g_used = 0, g_hi = 0, g_total = 0, g_tag_bytes = 0;

static void* bump(size_t bytes) {
    size_t a = (g_used + 7u) & ~((size_t)7u);      /* 8-byte align */
    if (a + bytes > POOL_BYTES) { fprintf(stderr, "POOL OOM (+%zu at %zu)\n", bytes, a); return NULL; }
    void* p = g_pool + a;
    g_used = a + bytes;
    if (g_used > g_hi) g_hi = g_used;
    g_total += bytes; g_tag_bytes += bytes;
    return p;
}
void* amb_calloc(size_t n, size_t sz) { size_t b = n * sz; void* p = bump(b); if (p) memset(p, 0, b); return p; }
void* amb_malloc(size_t sz)           { return bump(sz); }
void* amb_realloc(void* p, size_t sz) { (void)p; return bump(sz); }   /* harness only: no shrink/reuse */
void  amb_free(void* p)               { (void)p; }                     /* bump allocator: no free */

/* ---------------------- tiny WAV writer (PCM16 stereo) --------------------- */
static void write_wav(const char* path, const float* L, const float* R, int frames, int sr) {
    FILE* f = fopen(path, "wb"); if (!f) { perror("wav"); return; }
    int32_t datalen = frames * 2 * 2, riff = 36 + datalen, srr = sr, byterate = sr * 2 * 2, fmtlen = 16;
    int16_t fmt = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&srr, 4, 1, f); fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&datalen, 4, 1, f);
    for (int i = 0; i < frames; i++) {
        float l = L[i] < -1 ? -1 : L[i] > 1 ? 1 : L[i];
        float r = R[i] < -1 ? -1 : R[i] > 1 ? 1 : R[i];
        int16_t sl = (int16_t)(l * 32767.f), sr2 = (int16_t)(r * 32767.f);
        fwrite(&sl, 2, 1, f); fwrite(&sr2, 2, 1, f);
    }
    fclose(f);
}

/* ------------- test input: four decaying-sine "plucks" (the synth) -------- */
static void make_input(float* L, float* R, int frames, int sr) {
    for (int i = 0; i < frames; i++) { L[i] = R[i] = 0.f; }
    const double notes[4] = { 261.63, 329.63, 392.00, 523.25 };  /* C4 E4 G4 C5 */
    for (int n = 0; n < 4; n++) {
        int start = (int)(n * 0.9 * sr);
        for (int i = 0; i < sr * 2 && start + i < frames; i++) {
            double t = (double)i / sr;
            double s = sin(2.0 * M_PI * notes[n] * t) * exp(-t * 2.5) * 0.5;
            L[start + i] += (float)s; R[start + i] += (float)s;
        }
    }
}

#define SR    32000
#define SECS  15
#define TOTAL (SR * SECS)
static float inL[TOTAL], inR[TOTAL], outL[TOTAL], outR[TOTAL];

int main(void) {
    printf("=== Ambiotica @ %d Hz — fixed-arena port [%s] ===\n\n", SR, VARIANT);
    make_input(inL, inR, TOTAL, SR);

    looper_t* l; granular_t* g; microloop_t* m; reverb_t* r;
    const int loopcap = 32 * SR;   /* kLoopBufMaxSeconds(32) * sr — matches the plugin */

    printf("module buffers (allocated at create; this is the Plinky RAM budget):\n");
    #define CREATE(tag, expr) do { g_tag_bytes = 0; expr; \
        printf("  %-10s %9.1f KB\n", tag, g_tag_bytes / 1024.0); } while (0)
    CREATE("looper",    l = looper_create(loopcap, SR));
    CREATE("granular",  g = granular_create(SR));
    CREATE("microloop", m = microloop_create(SR));
    CREATE("reverb",    r = reverb_create(SR));
    #undef CREATE
    printf("  ----------------------\n");
    printf("  TOTAL      %9.1f KB   (%.2f MB)\n", g_total / 1024.0, g_total / 1048576.0);
    printf("  -> PSRAM usable ~7.9 MB: %s\n", (g_total <= 7.9 * 1048576) ? "FITS" : "OVER BUDGET (must cap loop length)");

    ambt_params p; memset(&p, 0, sizeof p);
    p.mix = 0.7f; p.loop_layer = 0.5f; p.grain_size = 0.4f; p.scatter = 0.5f;
    p.micro_hold = 0.3f; p.decay = 0.6f; p.mod_depth = 0.3f; p.mod_rate = 0.4f;
    p.mod_shape = 0; p.mod_sync = 0; p.lofi = 0; p.loop_length_bars = 2.0f; p.bpm = 120.f;

    clock_t t0 = clock();
    ambt_render_chain(l, g, m, r, &p, SR, inL, inR, outL, outR, TOTAL);
    double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("\nrender: %.1fs audio in %.3fs CPU  => %.0fx realtime (desktop, not RP2350)\n",
           (double)SECS, cpu, SECS / cpu);

    write_wav(WAVNAME, outL, outR, TOTAL, SR);
    printf("wrote %s (%ds, %d Hz stereo)\n", WAVNAME, SECS, SR);
    return 0;
}
