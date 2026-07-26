/* Euclidean rhythm generation for the drums page.
 *
 * Kept out of panel.cpp so it can actually be tested: panel.cpp has no #includes and only
 * type-checks after amalgamation, and the DSP harness never builds it. amalgamate.sh inlines
 * this header ahead of panel.cpp (the #include below is stripped there, exactly like every
 * other module header); euclid_test.c includes it directly. See euclid_test.sh.
 */
#ifndef AMBIOTICA_EUCLID_H
#define AMBIOTICA_EUCLID_H

#include "../dsp/drums.h"    /* DRUM_STEPS - stripped by amalgamate.sh */

/* Write a k-pulse Euclidean rhythm over DRUM_STEPS steps into dst, rotated right by rot.
 *
 * Bresenham/bucket form. Maximal evenness is what a Euclidean rhythm IS, so this lands on
 * the same necklace as Bjorklund without the recursion or the scratch arrays - which is why
 * there is no allocation here and no reason for any.
 *
 * The pulse count is exactly k for every k in 1..DRUM_STEPS, including those sharing a
 * factor with 16: as i runs over Z16, (i*k) mod 16 hits each multiple of g = gcd(k,16)
 * exactly g times, and k/g of those multiples fall below k, giving (k/g)*g = k. k=6 and
 * k=10 are where a naive "every 16/k-th step" generator drifts; euclid_test.c asserts all 16.
 *
 * rot may be negative or beyond DRUM_STEPS; the mask wraps it either way (two's complement,
 * mandated by C23 and true on the RP2350), the same idiom drum_step already uses in panel.cpp.
 *
 * Pulses are written as 127 - "always fires" - matching what hand-painting a step writes, so
 * a generated track and a drawn one are indistinguishable to fire_drum_step and to PROB.
 * Rests are written as 0, which fully replaces whatever the track held before.
 */
static inline void euclid_fill(unsigned char* dst, int k, int rot) {
    for (int i = 0; i < DRUM_STEPS; i++) {
        int j = (i - rot) & (DRUM_STEPS - 1);
        dst[i] = ((j * k) % DRUM_STEPS) < k ? 127 : 0;
    }
}

#endif
