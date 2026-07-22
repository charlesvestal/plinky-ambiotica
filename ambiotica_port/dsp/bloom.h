/* Ambiotica bloom — input "attack removal" / auto-swell.
 *
 * A continuous transient suppressor: a fast amplitude follower and a
 * slow-attack follower run in parallel; the applied gain is the ratio
 * slow/fast (clamped to 1). A transient spikes the fast follower while the
 * slow one lags, so the onset is ducked and then swells in as the slow
 * follower catches up. Decays pass through unchanged, so reverb tails are
 * preserved. The result turns plucked/struck/percussive input into bowed,
 * blooming pads.
 *
 *   amount = 0  → bypass (slow follower == fast follower, gain == 1)
 *   amount up   → longer swell (slow-follower attack 2 ms … ~2 s)
 *
 * Realtime contract: process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_BLOOM_H
#define AMBIOTICA_BLOOM_H

typedef struct bloom_s bloom_t;

bloom_t* bloom_create(double sample_rate);
void     bloom_destroy(bloom_t *b);
void     bloom_reset(bloom_t *b);

/* 0 = bypass, 1 = maximum swell. */
void     bloom_set_amount(bloom_t *b, float amount_0_1);

void     bloom_process(bloom_t *b,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r,
                       int frames);

#endif
