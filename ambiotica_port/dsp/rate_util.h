/* Ambiotica sample-rate scaling helpers (Phase 1).
 *
 * The DSP was authored with all time-based constants expressed as sample
 * counts / one-pole coefficients at 44.1 kHz. These helpers rescale those
 * reference values to the host rate.
 *
 * Bit-for-bit parity contract: at exactly 44.1 kHz every helper returns the
 * original reference value unchanged (the `== AMB_REF_SAMPLE_RATE` guard),
 * so 44.1 kHz output is identical to the pre-refactor DSP.
 */
#ifndef AMBIOTICA_RATE_UTIL_H
#define AMBIOTICA_RATE_UTIL_H

#include <math.h>

#define AMB_REF_SAMPLE_RATE 44100.0

/* A sample count defined at 44.1 kHz, rescaled to `sr` and rounded to nearest.
 * Exact at the reference rate (no rounding drift). */
static inline int amb_scale_samples(double ref_samples_at_44k, double sr) {
    if (sr == AMB_REF_SAMPLE_RATE) return (int)(ref_samples_at_44k + 0.5);
    return (int)(ref_samples_at_44k * (sr / AMB_REF_SAMPLE_RATE) + 0.5);
}

/* Same, keeping fractional precision — for sub-sample quantities like the
 * reverb's modulation depth in samples. */
static inline float amb_scale_samples_f(double ref_samples_at_44k, double sr) {
    if (sr == AMB_REF_SAMPLE_RATE) return (float)ref_samples_at_44k;
    return (float)(ref_samples_at_44k * (sr / AMB_REF_SAMPLE_RATE));
}

/* Rescale a one-pole coefficient a = exp(-2*pi*fc/fs) to a new rate while
 * preserving its cutoff / time constant:  a' = a ^ (ref_sr / sr).
 * Exact at the reference rate. */
static inline float amb_scale_onepole(float ref_coef_at_44k, double sr) {
    if (sr == AMB_REF_SAMPLE_RATE) return ref_coef_at_44k;
    return powf(ref_coef_at_44k, (float)(AMB_REF_SAMPLE_RATE / sr));
}

#endif /* AMBIOTICA_RATE_UTIL_H */
