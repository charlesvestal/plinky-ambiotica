/* Ambiotica drums — an x0x-style kit that deliberately sits OUTSIDE the wash.
 *
 * The panel owns its audio output, so drums are rendered into the chain's OUTPUT buffers
 * after fc_render_block rather than into its input. They therefore never reach the looper,
 * the grains or the plate: the pattern stays dry and legible under an ambient wash that is
 * doing whatever it likes. That placement is the whole design, not an implementation detail.
 *
 * The kit is SYNTHESISED once at create() into 8-bit mono buffers in PSRAM. No sample files,
 * so the panel stays a single self-contained .cpp, boots instantly and has nothing for a user
 * to install. 8-bit is half the PSRAM read traffic of 16 — which matters here, because core1
 * reaches PSRAM over the same QSPI bus it fetches code through (see PORT_NOTES) — and the
 * grit suits the material. Whole kit is ~57 KB.
 *
 * Realtime contract: drums_render and drums_trigger allocate nothing and do no I/O.
 * Patterns are NOT stored here — they live in the panel so they serialise with the scene.
 */
#ifndef AMBIOTICA_DRUMS_H
#define AMBIOTICA_DRUMS_H

enum {
    DRUM_KICK, DRUM_SNARE, DRUM_CHAT, DRUM_OHAT,
    DRUM_CLAP, DRUM_RIM,   DRUM_TOMLO, DRUM_TOMHI,
    DRUM_TRACKS
};
#define DRUM_STEPS 16

/* NOT a PLINKY_DSP_RAM_FUNC. Tried and measured: no change at all (~180us before and
 * after), because this loop is DATA bound, not instruction bound — it waits on paged
 * sample reads, so moving its code out of the XIP cache changes nothing. The SRAM code
 * region is scarce and shared with granular/looper/microloop/dattorro, where it does pay.
 * The lever for this function is fewer and cheaper reads, not faster code fetch. */

typedef struct drums_s drums_t;

/* Synthesises the kit. Allocates from the caller's current arena region — put it in PSRAM. */
drums_t* drums_create(double sample_rate);

/* velocity 1..127; 0 is ignored. Retriggering a track restarts it (one voice per track, as
   on the machines this imitates — a closed hat cuts the open hat, see drums.c). */
void drums_trigger(drums_t* d, int track, int velocity);

/* Point a track at sample data owned by the preset system, as a virtual-address range (see
   READ_SAMPLE / get_mip_va in the SDK). Pass va_end <= va_start to clear it, and the track
   falls back to its generated voice — which is what keeps the panel playable with no samples
   loaded at all. Slice bounds must come from the preset: the docs warn that reading past the
   end of a sample can crash, and that regions outside a loaded preset may not be resident. */
void drums_set_sample(drums_t* d, int track, unsigned int va_start, unsigned int va_end);

/* ADDS into out_l/out_r — does not clear them. Call after the wash has been rendered. */
void drums_render(drums_t* d, float* out_l, float* out_r, int frames);

/* Transpose the whole kit, in semitones (clamped +/-24). This is resampling, so a break
   pitched up gets shorter as well as higher — which is the point when you are chopping. */
void drums_set_pitch(drums_t* d, float semitones);

/* Closed hat cuts the open hat. On by default — it is what makes a kit sound like a kit,
   and exactly wrong for a sliced break, where the tracks are consecutive eighths of one
   continuous performance and would cut each other off. */
void drums_set_choke(drums_t* d, int enabled);

/* Silence every voice without clearing the kit (transport stop, scene load). */
void drums_all_off(drums_t* d);

#endif
