/* NEW PHRASE - hold x + REC.
 *
 * Empties the loop and micro-loop on contact and leaves the plate ringing, so a new phrase
 * can start over the tail of the old one. Event Horizon already clears the buffers, but only
 * at the end of a slider ride that collapses the whole engine first; this is the same
 * emptiness without the collapse.
 *
 * REC is col 12 row 15, the printed record circle, immediately left of x. "x + the record pad
 * wipes the recording" is stock Chords grammar - the manual says x "lets you reset or delete
 * individual things ... if you hold it and tap one of the other pads", and the MIDI chord
 * flow says "you can tap the X pad to wipe your recording". On this panel the rolling looper
 * IS the recording.
 *
 * Only the TIMING lives here, because only the timing is worth testing:
 *
 *   contact          -> stamp the loop and micro-loop clear (see drain.h)
 *   held  < 900 ms   -> keep stamping, so the buffer stays empty while the tail rings
 *   held  > 900 ms   -> tail_kill ramps 0..1 over 1200 ms, collapsing the plate
 *
 * Kept out of panel.cpp so it can be tested: panel.cpp has no #includes, only type-checks
 * after amalgamation, and the DSP harness never builds it. amalgamate.sh inlines this header
 * ahead of panel.cpp; newphrase_test.c includes it directly. See tests.sh.
 */
#ifndef AMBIOTICA_NEWPHRASE_H
#define AMBIOTICA_NEWPHRASE_H

/* How long the pad has to be down before the plate starts to go, and how long the collapse
   then takes. A stab clears the loop; a deliberate press and wait, about 2.1 s, is a full
   stop. Long enough that no ordinary clear can trip it by accident, short enough to be a
   performance move rather than a wait. */
#define NP_TAIL_HOLD_US   900000u
#define NP_TAIL_FADE_US  1200000u

typedef struct {
    unsigned      held_us;   /* time since the press; 0 when idle */
    unsigned char active;    /* 1 between press and release */
} newphrase_t;

static inline void newphrase_press(newphrase_t *np)   { np->active = 1; np->held_us = 0; }
static inline void newphrase_release(newphrase_t *np) { np->active = 0; np->held_us = 0; }

/* Advance the hold. SATURATES rather than wraps: this is an unsigned counter fed by the UI
   frame delta, and a pad leant on for long enough would otherwise overflow back through the
   threshold and bring the reverb up under a finger that never moved. The check is done on
   the remaining headroom, not on the sum, so held_us + dt_us is never computed and there is
   nothing left to overflow. dt_us is unsigned on purpose - it is what keeps that headroom
   check clean - so a caller reading a signed UI tick delta must clamp negative values to 0
   before calling; a raw cast would wrap a negative delta to a huge one and slam the hold
   straight to its cap. */
static inline void newphrase_tick(newphrase_t *np, unsigned dt_us) {
    if (!np->active) return;
    const unsigned cap = NP_TAIL_HOLD_US + NP_TAIL_FADE_US;
    if (np->held_us >= cap) return;
    if (dt_us >= cap - np->held_us) { np->held_us = cap; return; }
    np->held_us += dt_us;
}

/* 1 while the pad is held - that is the whole of the state this header tracks. The DSP side
   re-marks the loop and micro-loop clear every block for as long as this reads true, rather
   than firing once on the press edge, for the same reason Event Horizon re-stamps at the
   bottom of its slider: holding means "stay empty", and releasing starts recording from
   empty. */
static inline int newphrase_held(const newphrase_t *np) { return np->active ? 1 : 0; }

/* 0..1 target for the plate collapse. A TARGET, not the applied value: the DSP one-poles
   toward it, so releasing mid-fade returns the reverb send to unity smoothly instead of
   stepping it. Against a 1200 ms ramp that lag is inaudible. */
static inline float newphrase_tail_kill(const newphrase_t *np) {
    if (!np->active || np->held_us <= NP_TAIL_HOLD_US) return 0.0f;
    const unsigned into = np->held_us - NP_TAIL_HOLD_US;
    if (into >= NP_TAIL_FADE_US) return 1.0f;
    return (float)into / (float)NP_TAIL_FADE_US;
}

#endif
