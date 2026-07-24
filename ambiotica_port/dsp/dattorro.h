/* Dattorro plate reverb (JAES 1997, "Effect Design Part 1" — Griesinger figure-8
 * tank). Chosen over parallel combs for lushness-per-byte: each in-loop allpass
 * multiplies echo density for only its own delay line, so density grows far faster
 * per allocated sample than in a comb bank, and there is no comb periodicity to
 * expose as a "broken piano".
 *
 * Runs at HALF the host rate (16 kHz for the Plinky's 32 kHz) so the delay lines
 * fit SRAM (~48 KB float vs ~96 KB at full rate); reverb tails are dark so the
 * bandwidth loss is inaudible. dattorro_process() decimates 32->16k in and linearly
 * upsamples 16->32k out.
 *
 * The two decay-diffusion-1 allpasses are modulated by slow ~1 Hz LFOs (interpolated
 * reads) to break the tank's metallic ringing — cheap in CPU, free in memory. */
#ifndef AMB_DATTORRO_H
#define AMB_DATTORRO_H

typedef struct dattorro_s dattorro_t;

dattorro_t* dattorro_create(double sample_rate);
void        dattorro_destroy(dattorro_t* d);
void        dattorro_reset(dattorro_t* d);

/* decay 0..1 -> tail length (RT60 grows toward "infinite" near 1, but capped short
 * of self-oscillation). */
void        dattorro_set_decay(dattorro_t* d, float decay_0_1);
/* damp 0..1 -> high-frequency damping in the tank feedback (0 = bright, 1 = dark). */
void        dattorro_set_damp(dattorro_t* d, float damp_0_1);
/* mod 0..1 -> tank-allpass modulation depth (chorusy shimmer / de-metallise). */
void        dattorro_set_mod(dattorro_t* d, float mod_0_1);

/* Full host-rate stereo in -> stereo wet out (n frames). Internally half-rate. */
void        dattorro_process(dattorro_t* d, const float* in_l, const float* in_r,
                             float* out_l, float* out_r, int n);

#endif
