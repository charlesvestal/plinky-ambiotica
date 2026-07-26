/* The two per-step CONDITION controls, and whether a step fires.
 *
 * Chords prints MODULO and PROB side by side under CONDITION, and its manual keeps them
 * distinct: "You use Modulo to have individual steps only play once out of N times,
 * Probability to set a chance percentage of a single step playing." So they are two
 * independent settings per step here too, not one merged ring - a hit can be 50% AND 2:4.
 *
 *   PROB    1..127   fires if a d127 roll lands under it; 127 = always
 *   MODULO  0..9     0 = none, else 1+index into kConds - "play the Ath time of every B"
 *
 * Kept out of panel.cpp so it can be tested: panel.cpp has no #includes, only type-checks
 * after amalgamation, and the DSP harness never builds it. amalgamate.sh inlines this header
 * ahead of panel.cpp; stepcond_test.c includes it directly. See tests.sh.
 */
#ifndef AMBIOTICA_STEPCOND_H
#define AMBIOTICA_STEPCOND_H

#include "../dsp/drums.h"    /* DRUM_STEPS - stripped by amalgamate.sh */

/* PER-TRACK LENGTH. Tracks share one monotonic tick and derive their own position from it,
   rather than each keeping a counter - so shortening a track cannot drift it out of phase
   with the others, and there is no per-track state to reset or serialise beyond the length
   itself. Unequal lengths realign at their least common multiple: 16 against 7 is 112 ticks,
   which is the whole point of the feature. */

/* A length of 0 would divide by zero, and a scene saved before this feature existed carries
   no "dlen" field, so the array can legitimately arrive empty or stale. Anything out of
   range reads as the full bar, which is exactly the old behaviour. */
static inline unsigned int track_len(unsigned char len) {
    return (len >= 1u && len <= (unsigned char)DRUM_STEPS) ? len : (unsigned int)DRUM_STEPS;
}

static inline unsigned int step_of(unsigned int tick, unsigned char len) {
    return tick % track_len(len);
}

/* Which time round ITS OWN loop this track is on - so MODULO's "2nd of every 4" counts four
   cycles of a 7-step track, not four bars it never agreed to. */
static inline unsigned int pass_of(unsigned int tick, unsigned char len) {
    return tick / track_len(len);
}

/* Ordered by cycle length, which is the order MODULO's ring walks them in. Capped at 4 loops
   because the ring is stepped by tapping - longer cycles would want the manual's two-slider
   editor instead. */
typedef struct { unsigned char a, b; } step_cond;
static const step_cond kConds[] = {
    {1,2},{2,2},
    {1,3},{2,3},{3,3},
    {1,4},{2,4},{3,4},{4,4},
};
#define COND_N ((int)(sizeof kConds / sizeof kConds[0]))

static inline const step_cond* cond_of(int i) { return &kConds[i]; }

/* PROB's ring: 100 -> 75 -> 50 -> 25 -> 100. Unchanged by the arrival of MODULO, and it never
   returns 0 - cycling changes how a hit decides, it does not delete it. */
static inline unsigned char next_prob(unsigned char p) {
    return p >= 127 ? 96 : p >= 96 ? 64 : p >= 64 ? 32 : 127;
}

/* MODULO's ring: off -> 1:2 -> 2:2 -> 1:3 -> ... -> 4:4 -> off. "off" is in the ring so a
   condition can be taken back off a step without erasing and redrawing the step itself. */
static inline unsigned char next_mod(unsigned char m) {
    return (unsigned char)(m >= (unsigned char)COND_N ? 0 : m + 1);
}

/* Labels are built by hand rather than with snprintf: the panel runtime is not a hosted C
   library, and these are at most three characters. Four is all that fits across 16 columns
   in FONT_4, which every label here respects. */
static inline const char* prob_label(unsigned char p) {
    static char buf[8];
    int pct = (p * 100) / 127;          /* 127/96/64/32 -> 100/75/50/25 */
    if (pct >= 100) { buf[0] = '1'; buf[1] = '0'; buf[2] = '0'; buf[3] = 0; }
    else            { buf[0] = (char)('0' + pct / 10); buf[1] = (char)('0' + pct % 10); buf[2] = 0; }
    return buf;
}

static inline const char* mod_label(unsigned char m) {
    static char buf[8];
    if (!m) { buf[0] = '-'; buf[1] = '-'; buf[2] = 0; return buf; }
    const step_cond* c = &kConds[m - 1];
    buf[0] = (char)('0' + c->a); buf[1] = ':'; buf[2] = (char)('0' + c->b); buf[3] = 0;
    return buf;
}

/* Is this step due on the current pass, before the dice are even considered? Split out so the
   grid can light a modulo'd step brightly on the pass it will fire and dimly on the ones it
   will not - otherwise 1:4 and 2:4 are indistinguishable on the surface. */
static inline int mod_due(unsigned char m, unsigned int pass) {
    if (!m) return 1;
    const step_cond* c = &kConds[m - 1];
    return (int)(pass % c->b) == c->a - 1;
}

/* Does this step fire? `pass` counts completed loops of the pattern; `roll` is a d127 the
   caller supplies, which keeps this pure and therefore testable - the RNG stays in the panel.
   Both conditions must pass, and modulo is evaluated first because it is deterministic: a
   step that is not due this pass must not burn a roll deciding something it cannot do. */
static inline int step_fires(unsigned char p, unsigned char m, unsigned int pass, unsigned int roll) {
    if (!p) return 0;
    if (!mod_due(m, pass)) return 0;
    if (p < 127 && roll % 127u >= p) return 0;
    return 1;
}

#endif
