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
/* Empty the loop. Does BOTH halves of the job without ever blocking:
     - immediately, reads stop reaching back past this moment, so the loop goes silent at once
     - then the ring is genuinely zeroed by a sweep running in the audio thread, a handful of
       samples per sample, backward from the write head so the readable window clears first
   A straight memset here would move ~4 MB of PSRAM from core0 and starve core1 over the
   shared QSPI bus - measured at a 658 ms audio block - and slicing it across UI frames only
   turns one long underrun into several short ones. */
void      looper_clear(looper_t *l);

/* Process stereo block. Reads from in_l/in_r and writes them into the
 * capture ring (with feedback). out_l/out_r receive ONLY the loop signal
 * (fb × delayed_read) - no dry. The caller is responsible for mixing dry
 * back in. in and out may NOT alias. */
void      PLINKY_DSP_RAM_FUNC(looper_process)(looper_t *l,
                         const float *in_l, const float *in_r,
                         float *out_l, float *out_r,
                         int frames);


#endif
