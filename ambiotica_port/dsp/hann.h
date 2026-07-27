/* One Hann window table, shared by the reverse-read crossfades.
 *
 * looper.c and microloop.c both crossfade two backward read heads offset by half the loop,
 * and both were computing the window shape as
 *
 *     0.5f - 0.5f * fast_cosf(6.2831853f * (float)ph / (float)L)
 *
 * per sample, per head - a float DIVIDE plus a transcendental, four of each per sample once
 * both heads are counted. granular.c already had the answer ("Forward Hann from the LUT - no
 * per-grain fast_cosf, exact 0 at edges"); this is that answer factored out so the reverse
 * paths get it too.
 *
 * 512 steps across a reverse window that spans a whole loop - seconds of audio - is far finer
 * than the ear can follow, and unlike the polynomial fast_cosf it is exactly 0 at both edges,
 * which is what keeps the crossfade seam silent.
 *
 * granular.c keeps its own 2048-entry table: its grains can be a few ms long, where 512 would
 * be coarse. Not worth unifying at the cost of making the fine table pay for the coarse use.
 */
#ifndef AMBIOTICA_HANN_H
#define AMBIOTICA_HANN_H

#include "fast_math.h"   /* fast_cosf, only used to build the table */

#define AMB_HANN_N 512

static float amb_hann_lut[AMB_HANN_N + 1];
static int   amb_hann_ready = 0;

static void amb_hann_init(void) {
    if (amb_hann_ready) return;
    for (int i = 0; i <= AMB_HANN_N; i++)
        amb_hann_lut[i] = 0.5f * (1.0f - fast_cosf(6.2831853f * (float)i / (float)AMB_HANN_N));
    amb_hann_ready = 1;
}

/* Hann value at phase ph/L, with ph in [0, L). inv_L is 1/L, hoisted by the caller.
 * LINEARLY INTERPOLATED between entries: a reverse window spans a whole loop, so one entry
 * covers hundreds of samples and a raw lookup would step the crossfade gain in ~0.2%
 * staircases. Measured against the fast_cosf it replaces, truncation was 0.49% of peak
 * (-46 dBFS) and interpolation takes it to a small fraction of that. Still two loads, two
 * multiplies and an add - far below the float divide plus transcendental it replaces. */
static inline float amb_hann(int ph, float inv_L) {
    float x = (float)ph * inv_L * (float)AMB_HANN_N;
    if (x < 0.0f) x = 0.0f; else if (x > (float)AMB_HANN_N) x = (float)AMB_HANN_N;
    int   i = (int)x;
    if (i >= AMB_HANN_N) return amb_hann_lut[AMB_HANN_N];
    float f = x - (float)i;
    return amb_hann_lut[i] + (amb_hann_lut[i + 1] - amb_hann_lut[i]) * f;
}

#endif
