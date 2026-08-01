#ifndef AMB_DRAIN_H
#define AMB_DRAIN_H

/* Event Horizon drain cursor - shared by the looper and the micro-loop.
 *
 * Both hold a ring far larger than the window that is actually played back, so a drain only
 * has to erase `window` samples behind the read head. The obvious way to spend a fixed budget
 * of stores per sample is a cursor, and the obvious cursor is an offset back from the write
 * head. That version is what shipped, and it does not work:
 *
 *     idx = write_pos - off,  write_pos += 1/sample,  off += per/sample
 *     => idx moves at (per - 1) slots per sample
 *
 * At per = 1 the cursor is FROZEN against the buffer - it rewrites one address forever. At
 * per = 2 it crawls at one slot per sample, so a 4 s / 128000-sample loop needs a full 4 s of
 * holding to make one pass. Hold for three seconds and a quarter of the loop survives, which
 * is exactly what came back on the way up. Half the stores were landing on the slot the
 * previous sample had already cleared.
 *
 * So the cursor is ABSOLUTE, and it starts at the READ HEAD:
 *
 *   - Anchored at the read head, the cursor and playback begin at the same sample and the
 *     cursor moves `per` times faster, so from the second sample on, playback is reading
 *     memory that has already been cleared. No lead-in during which the old loop is audible.
 *   - Ascending, so the prefetcher sees a straight line. The region between the anchor and
 *     the write head is the only part that is ever read again; the write head is laying
 *     silence over the far end at one sample per sample for free.
 *   - Coverage is the full `per` slots per sample, plus the write head's one. A `window`-sized
 *     region is therefore clear after window/(per+1) samples: 1.3 s for a 4 s loop at per = 2,
 *     against the 4 s (or never) it was.
 *
 * A pass that completes while the drain is still deepening re-anchors and runs again, so a
 * partial dip that is later pushed to the bottom gets the deeper decay rather than keeping
 * whatever the first pass left. */
/* Stores per sample. Two, and NOT one, even at shallow drains where one would be cheaper:
 * the cursor has to move strictly FASTER than the read head or it never gets in front of it.
 * At one per sample it advances in lockstep with playback, clearing each sample just after it
 * has been heard - a whole loop plays at full volume before anything goes quiet. At two it is
 * ahead from the first sample and the old loop is never audible at all.
 *
 * Two is also the ceiling. Measured on device: 8 per sample put STG loop at ~650 against a
 * 190 baseline, and dragged granular, micro and reverb up with it, because they share the
 * PSRAM bus - dsp hit 2850 us and glitched. The chain already peaks near 1800-2130 us with no
 * drain at all, so the affordable budget is about 100 us. */
#define LEAK_PER_SAMPLE 2

typedef struct {
    int pos;    /* absolute buffer index the next store lands on */
    int left;   /* samples still to clear in this pass; <= 0 re-anchors */
} drain_cursor_t;

/* Force a re-anchor on the next store. Call when a drain begins, so a stale cursor from an
 * earlier gesture cannot resume mid-pass against a window that has since moved. */
static inline void drain_restart(drain_cursor_t *dc) { dc->left = 0; }

/* Next absolute index to clear. `read_pos` is the current read head, used only when a pass
 * ends and the cursor re-anchors. */
static inline int drain_next(drain_cursor_t *dc, int read_pos, int window, int cap) {
    if (dc->left <= 0) { dc->pos = read_pos; dc->left = window; }
    int i = dc->pos;
    if (++dc->pos >= cap) dc->pos = 0;
    dc->left--;
    return i;
}

#endif
