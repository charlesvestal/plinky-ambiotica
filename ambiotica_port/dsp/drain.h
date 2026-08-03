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
    int pos;          /* absolute buffer index the next store lands on */
    int left;         /* samples still to clear in this pass; <= 0 re-anchors */
    int since_clear;  /* samples recorded since the buffer was declared empty */
} drain_cursor_t;

/* THE CLEAR MARK.
 *
 * A sweep alone cannot empty these buffers, for two reasons that no amount of stores per
 * sample fixes:
 *
 *   - It only runs while the gesture does. It clears a finite region ahead of the read head,
 *     and when the slider comes back up the cursor stops while the read head keeps walking -
 *     straight off the end of the cleared region and back into old audio. Measured: hold ~2.5
 *     s, and the loop returns about 4 s after release.
 *   - It cannot cover every read. The Dilate heads read as far as TWO loop lengths back
 *     (rabs = rev_base - ph, ph up to loop_len, base already loop_len behind), which is
 *     outside any window sized on loop_len.
 *
 * So emptiness is recorded rather than performed. `since_clear` counts samples recorded since
 * the buffer was declared empty; a read `age` samples behind the write head predates the
 * clear, and returns silence, whenever age > since_clear. That is instant, total whatever the
 * loop length, correct for every tap including the reverse heads, and CHEAPER than the read
 * it replaces - it skips the PSRAM fetch instead of adding one.
 *
 * The sweep is kept and still runs, so the bytes genuinely do go to zero rather than merely
 * becoming unreachable. It is now hygiene, not the mechanism. */
static inline void drain_mark_clear(drain_cursor_t *dc) { dc->since_clear = 0; }

/* BORN EMPTY IS NOT DECLARED EMPTY.
 *
 * A freshly created ring is calloc'd, so its buffer reads as silence and `since_clear` starts
 * at 0 - bit-identical to the state `drain_mark_clear` produces. That is fine for reads, which
 * correctly skip the fetch either way, but callers that treat staleness as "the user just
 * cleared this" must not fire at power-on. Seed a new ring as fully recorded so only an
 * explicit clear makes it stale. */
static inline void drain_init_recorded(drain_cursor_t *dc, int cap) { dc->since_clear = cap; }

/* One recorded sample. Saturates at the ring size: past that everything is post-clear and the
 * counter must not wrap back into the stale range. */
static inline void drain_tick(drain_cursor_t *dc, int cap) {
    if (dc->since_clear < cap) dc->since_clear++;
}

/* True if a read `age` samples behind the write head is older than the last clear. */
static inline int drain_stale(const drain_cursor_t *dc, int age) {
    return age > dc->since_clear;
}

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
