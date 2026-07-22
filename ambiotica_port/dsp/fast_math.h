/* Fast float approximations of transcendentals, for the RP2350 (Cortex-M33 has
 * no hardware trig; software sinf/cosf/tanhf/powf cost hundreds of cycles each,
 * which blows the ~2 ms core0 DSP budget when called per-sample). Accuracy here
 * is "inaudible in a modulation/window/waveshaper", not scientific.
 *
 * Used to replace ONLY the per-sample (hot-loop) calls in the chain. Per-block
 * / setup calls (reverb_set_*, *_create) keep the exact libm versions.
 */
#ifndef AMB_FAST_MATH_H
#define AMB_FAST_MATH_H

/* tanh via Padé-3, bounded to +/-1 (same shape as looper's soft_sat). ~5 flops. */
static inline float fast_tanhf(float x) {
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* sin via range-reduction to [-pi,pi] + odd 5th-order minimax poly (~1e-3). */
static inline float fast_sinf(float x) {
    const float TWO_PI = 6.28318530718f, INV_TWO_PI = 0.15915494309f;
    float k = x * INV_TWO_PI;
    k = k - (float)(int)(k + (k >= 0.0f ? 0.5f : -0.5f));   /* frac to [-0.5,0.5] */
    float t = k * TWO_PI;                                    /* t to [-pi,pi] */
    float t2 = t * t;
    return t * (0.9999966f + t2 * (-0.1666568f + t2 * 0.0083143f));
}
static inline float fast_cosf(float x) { return fast_sinf(x + 1.57079632679f); }

/* 2^x approx (only needed by granular pitch mod). Bit-trick exponent + poly. */
static inline float fast_exp2f(float x) {
    if (x >  30.0f) x =  30.0f;
    if (x < -30.0f) x = -30.0f;
    float fl = (float)(int)(x >= 0.0f ? x : x - 1.0f);      /* floor */
    float xf = x - fl;
    float p = 1.0f + xf * (0.6931472f + xf * (0.2402265f + xf * 0.0555041f));
    union { float f; unsigned int u; } v; v.f = p;
    v.u += ((unsigned int)(int)fl) << 23;                   /* * 2^floor(x) */
    return v.f;
}

#endif
