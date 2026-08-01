/* Ambiotica micro-loop - Stage 3 of the chain.
 *
 * 4-second stereo buffer. Runs parallel to the looper/granular path:
 * captures whatever input is fed in (typically the dry signal) and outputs
 * a delayed/looped copy. Always functional regardless of Loop Layer.
 *
 *   hold = 0   → silent output (no loop content)
 *   hold low   → short stutter (~50–200 ms)
 *   hold mid   → ~1–2 s sustained layer
 *   hold high  → up to 4 s
 *   hold ≥ 0.95 OR freeze=1  → buffer locked, captured content keeps looping
 *
 * Output is ONLY the loop content (hold × delayed read). The caller adds
 * dry passthrough separately so the micro-loop can be an additive layer
 * in the wet bus.
 *
 * Realtime contract: process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_MICROLOOP_H
#define AMBIOTICA_MICROLOOP_H

/* microloop_process is one of the heaviest core1 stages (~240us/block, measured). PLINKY_DSP_RAM_FUNC un-inlines it and
 * places its code in SRAM, exempting it from the shared XIP cache. Fallback so the desktop
 * harness and older firmware still build. NOTE: the SRAM code region is small and
 * granular/looper already live there - if this stops LINKING, drop the wrapper. */
#ifndef PLINKY_DSP_RAM_FUNC
#define PLINKY_DSP_RAM_FUNC(f) f
#endif

typedef struct microloop_s microloop_t;

/* sample_rate scales the buffer, loop-length bounds, crossfade, and smoothing. */
microloop_t* microloop_create(double sample_rate);
void         microloop_destroy(microloop_t *m);
void         microloop_reset(microloop_t *m);

/* Event Horizon: decay the active micro-loop window by `factor` (per block) so
 * the buffer empties over time as Horizon is lowered (>=1 is a no-op). */
void         microloop_leak(microloop_t *m, float factor);

/* Event Horizon: bleed the captured micro-loop away as `amount` rises (0 = keep, 1 = erase).
   Matters more here than on the looper: fb_target floors at 0.55 even at hold 0, so without
   this the delay recirculates forever however far the slider falls. Scales the feedback down
   (free, and what actually stops the re-injection) and sweeps the buffer a couple of samples
   per sample. */
void         microloop_set_leak(microloop_t *m, float amount_0_1);

/* Dilate: 0 = forward, 1 = reversed (reverse-delay read of the micro-loop). */
void         microloop_set_reverse(microloop_t *m, float amount_0_1);

void         microloop_set_hold(microloop_t *m, float hold_0_1);

/* Set the micro-loop length in samples (host supplies a tempo-synced division).
 * Crossfades smoothly to the new length. */
void         microloop_set_loop_len(microloop_t *m, int len_samples);

void         PLINKY_DSP_RAM_FUNC(microloop_process)(microloop_t *m,
                               const float *in_l, const float *in_r,
                               float *out_l, float *out_r,
                               int frames);


#endif
