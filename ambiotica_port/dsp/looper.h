/* Ambiotica looper - Stage 1 of the chain.
 *
 * 30-second stereo ring buffer with always-on rolling capture. Loop Layer
 * knob maps to feedback gain (0..0.95), which controls both (a) how much
 * of the delayed read is fed back into the buffer and (b) how much is
 * blended into the output. At feedback = 0 the looper is transparent.
 *
 * Phase 4: forward playback only. Reverse direction (knob doubletap on
 * Grain Size in the design) arrives in phase 8.
 */
#ifndef AMBIOTICA_LOOPER_H
#define AMBIOTICA_LOOPER_H

/* looper_process is the biggest remaining core1 stage (~245us/block, measured). The SDK's
 * PLINKY_DSP_RAM_FUNC un-inlines it and places its code in SRAM, exempting it from the
 * shared XIP cache. Fallback so the desktop harness and older firmware still build.
 * NOTE: the SRAM code region is small - if this stops LINKING, drop the wrapper. */
#ifndef PLINKY_DSP_RAM_FUNC
#define PLINKY_DSP_RAM_FUNC(f) f
#endif

typedef struct looper_s looper_t;

/* Create with maximum buffer capacity in samples. The active loop length
 * defaults to this capacity and can be reduced via looper_set_loop_len.
 * sample_rate scales the internal time-based constants (feedback smoothing,
 * loop-length crossfade). Returns NULL on allocation failure. */
looper_t* looper_create(int buf_capacity_samples, double sample_rate);
void      looper_destroy(looper_t *l);
void      looper_reset(looper_t *l);

/* Event Horizon: decay the active loop window by `factor` (per block) so the
 * captured loop empties over time as Horizon is lowered (>=1 is a no-op). */
void      looper_leak(looper_t *l, float factor);

/* Dilate: 0 = forward output, 1 = reversed (reverse-delay read of the loop). */
void      looper_set_reverse(looper_t *l, float amount_0_1);

/* layer_0_1: 0 = no loop (transparent), 1 = ~unity feedback (long-tailed). */
void      looper_set_loop_len(looper_t *l, int loop_len_samples);
void      looper_set_layer(looper_t *l, float layer_0_1);

/* One-shot: zero the entire buffer. Safe to call from set_param. */
/* Hard clear: memsets the whole ring. ~4 MB of PSRAM, so it BLOCKS - only safe where a
   stall is acceptable (never from a live gesture; use looper_set_leak for that). */
void      looper_clear(looper_t *l);

/* Event Horizon: bleed the captured loop away as `amount` rises (0 = keep, 1 = erase what it
   touches). Applied continuously to a slice of the loop window per sample, so the loop decays
   as the slider falls and is empty by the bottom - no discrete clear, nothing to click, and
   no blocking memset. A quick dip thins the loop; holding at the bottom erases it. */
void      looper_set_leak(looper_t *l, float amount_0_1);

/* Event Horizon at the bottom: declare the loop empty. Instant, no memset, no stall - every
   tap older than this instant reads silence, including the Dilate heads that reach twice
   loop_len back and that no sweep window can cover. Call it every block while the slider is
   at the bottom; holding there keeps the loop silent, releasing lets it record again. */
void      looper_mark_clear(looper_t *l);

/* Process stereo block. Reads from in_l/in_r and writes them into the
 * capture ring (with feedback). out_l/out_r receive ONLY the loop signal
 * (fb × delayed_read) - no dry. The caller is responsible for mixing dry
 * back in. in and out may NOT alias. */
void      PLINKY_DSP_RAM_FUNC(looper_process)(looper_t *l,
                         const float *in_l, const float *in_r,
                         float *out_l, float *out_r,
                         int frames);


#endif
