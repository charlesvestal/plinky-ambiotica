/* Ambiotica harmony — tuned resonator bank ("comb the wash into a chord").
 *
 * A small bank of damped, tuned feedback combs (Karplus-Strong-style
 * resonators). Fed the broadband reverb/wet bus, each comb rings at a chord
 * frequency, pulling a sustained, smooth, pitched chord out of the wash — a
 * lush harmonic layer, not bleepy pitched grains.
 *
 *   amount = 0  → bypass (output == input)
 *   amount up   → more of the wash is replaced by the resonated chord
 *
 * Realtime contract: process performs no allocation and no I/O. Buffers are
 * sized in create(); set_chord only changes (fractional) delay lengths.
 */
#ifndef AMBIOTICA_HARMONY_H
#define AMBIOTICA_HARMONY_H

#define HARMONY_MAX_VOICES 3   /* 3-note chords; frees SRAM for the full chain */

typedef struct harmony_s harmony_t;

harmony_t* harmony_create(double sample_rate);
void       harmony_destroy(harmony_t *h);
void       harmony_reset(harmony_t *h);

/* Set the chord as a list of frequencies (Hz). n <= HARMONY_MAX_VOICES. */
void       harmony_set_chord(harmony_t *h, const float *freqs, int n);

/* 0 = bypass (dry wash), 1 = full resonated chord. */
void       harmony_set_amount(harmony_t *h, float amount_0_1);

/* 0 = shorter/plucky ring, 1 = long sustained chord. */
void       harmony_set_ring(harmony_t *h, float ring_0_1);

void       harmony_process(harmony_t *h,
                           const float *in_l, const float *in_r,
                           float *out_l, float *out_r,
                           int frames);

#endif
