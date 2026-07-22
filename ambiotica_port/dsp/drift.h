/* Ambiotica drift — slow stereo detune ("wow") for the wet bus.
 *
 * A pair of slowly-modulated fractional delay lines (left/right in quadrature)
 * impart a gentle, ever-wandering pitch drift of a few cents, so the texture
 * never sits perfectly still. Combined in the processor with reverb-tail
 * cross-feedback, one note blooms into an ever-shifting cloud.
 *
 *   amount = 0  → bypass (output == input)
 *   amount up   → deeper, wider wander (up to ~±10 cents peak)
 *
 * Realtime contract: process performs no allocation and no I/O. The delay
 * buffers are sized in create().
 */
#ifndef AMBIOTICA_DRIFT_H
#define AMBIOTICA_DRIFT_H

typedef struct drift_s drift_t;

drift_t* drift_create(double sample_rate);
void     drift_destroy(drift_t *d);
void     drift_reset(drift_t *d);

/* 0 = bypass, 1 = maximum wander. */
void     drift_set_amount(drift_t *d, float amount_0_1);

void     drift_process(drift_t *d,
                      const float *in_l, const float *in_r,
                      float *out_l, float *out_r,
                      int frames);

#endif
