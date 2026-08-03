# New Phrase Gesture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `× + REC`, a gesture that empties the loop and micro-loop on contact and leaves the plate ringing, so a new phrase can start over the tail of the old one.

**Architecture:** The clear itself already exists and is free (`looper_mark_clear` / `microloop_mark_clear` set `since_clear = 0`, and `drain_stale` returns silence for older reads without touching PSRAM). This plan adds three things around it: a pure-logic hold timer in a testable header, one scalar `tail_kill` in `fc_state` that collapses the plate's decay and input send on a long hold, and an edge-residual declick so the clear can land on the first available sample instead of waiting for a fade. It also repairs a latent slow-refill in the looper's write law that the gesture would otherwise expose.

**Tech Stack:** C11 (DSP + panel headers), C++ panel against the Plinky IDE SDK, `clang` for the desktop harness and unit tests, `sh` build scripts, single-file amalgamation for flashing.

**User decisions (already made):**
- Pad is `× + REC` (col 12 row 15), not `× + MUTE` and not `× + SCALE`. The user proposed REC; it is adjacent to `×` and matches the printed record circle.
- Hold behaviour: "hold to stay empty, and drop the tail too on a long hold" (the third option offered), chosen with the Event-Horizon-overlap caveat stated.
- "i want the gesture to clear immediately" - the clear is never delayed for declicking; the edge is smoothed after the fact instead.
- The refill fix is in scope, including the fact that it changes Event Horizon's release behaviour. The user said "go ahead" after that consequence was spelled out.
- Timings stay at 900 ms to the threshold and 1200 ms to full collapse. Offered for revision, user said "go ahead".

**Spec:** `docs/superpowers/specs/2026-08-03-new-phrase-gesture-design.md`

---

## File Structure

| File | Status | Responsibility |
| --- | --- | --- |
| `ambiotica_port/panel/newphrase.h` | Create | Pure hold-timer logic: press/release/tick to `held` + `tail_kill`. No SDK types |
| `ambiotica_port/panel/newphrase_test.c` | Create | Unit tests for the above |
| `ambiotica_port/harness/np_main.c` | Create | End-to-end driver over the real chain: refill, clear, tail collapse |
| `ambiotica_port/dsp/looper.c` | Modify | The `in_g` refill fix, ~line 244 and ~line 285 |
| `ambiotica_port/harness/full_chain.h` | Modify | `tail_kill` + edge residual in `fc_state`; applied to `rin`, decay and `wb` |
| `ambiotica_port/panel/panel.cpp` | Modify | `COL_REC`, gesture detect in `draw_nav`, tick in `on_ui`, drive in `on_dsp` |
| `ambiotica_port/panel/tests.sh` | Modify | Build and run `newphrase_test` |
| `ambiotica_port/panel/amalgamate.sh` | Modify | Inline `newphrase.h` ahead of `panel.cpp` |
| `ambiotica_port/harness/build.sh` | Modify | Build and run `amb_np_test` |

Task order is bottom-up so every task is independently verifiable on the desktop: pure logic, then DSP, then the panel wiring that cannot be desktop-tested.

---

### Task 1: New Phrase hold timer

**Goal:** A pure, tested header that turns "how long has `× + REC` been held" into a held flag and a 0..1 tail-collapse target.

**Files:**
- Create: `ambiotica_port/panel/newphrase.h`
- Create: `ambiotica_port/panel/newphrase_test.c`
- Modify: `ambiotica_port/panel/tests.sh:11`
- Modify: `ambiotica_port/panel/amalgamate.sh:43`

**Acceptance Criteria:**
- [ ] `newphrase_held` is 1 from the instant of press until release
- [ ] `newphrase_tail_kill` stays exactly 0 for the first 900 ms of hold
- [ ] It reaches 0.5 at 900 + 600 ms and 1.0 at 900 + 1200 ms
- [ ] It stays clamped at 1.0 for an arbitrarily long hold, never wrapping back toward 0
- [ ] Release returns both to their idle values
- [ ] `sh ambiotica_port/panel/tests.sh` passes with `-Wall -Wextra -Werror`

**Verify:** `sh ambiotica_port/panel/tests.sh` → ends with `check_order` output and no `FAIL` lines

**Steps:**

- [ ] **Step 1: Write the failing test**

Create `ambiotica_port/panel/newphrase_test.c`:

```c
/* Unit tests for the New Phrase hold timer (x + REC). A stab clears the loop; keeping the
   pad down holds the buffer empty; holding past a threshold also collapses the plate. Only
   the last of those is time-dependent, so it is the part that needs pinning. See
   newphrase.h. Run via tests.sh. */
#include <stdio.h>
#include <math.h>

#include "newphrase.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.001f)

/* A stab is the common case: the loop must be empty from the press, with the tail untouched. */
static void test_press_stamps_immediately_and_spares_the_tail(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    CHECK(newphrase_held(&np) == 1, "press did not register as held");
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "press already collapsing the tail: %.4f", (double)newphrase_tail_kill(&np));
}

/* The whole point of the threshold: a normal hold, however scruffy, must not touch the plate. */
static void test_short_hold_never_touches_the_tail(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US - 1000u);
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "tail collapsing 1 ms early: %.4f", (double)newphrase_tail_kill(&np));
    CHECK(newphrase_held(&np) == 1, "held dropped while pad still down");
}

static void test_tail_collapse_is_linear_across_the_fade(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US);
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "fade started before the threshold: %.4f", (double)newphrase_tail_kill(&np));
    newphrase_tick(&np, NP_TAIL_FADE_US / 2u);
    CHECK(NEAR(newphrase_tail_kill(&np), 0.5f),
          "half way through the fade reads %.4f, want 0.5", (double)newphrase_tail_kill(&np));
    newphrase_tick(&np, NP_TAIL_FADE_US / 2u);
    CHECK(NEAR(newphrase_tail_kill(&np), 1.f),
          "end of the fade reads %.4f, want 1.0", (double)newphrase_tail_kill(&np));
}

/* A pad leant on must saturate, not wrap - an unsigned overflow here would bring the reverb
   back up under a finger that never moved. */
static void test_a_very_long_hold_saturates(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    for (int i = 0; i < 20000; i++) newphrase_tick(&np, 20000u);   /* 400 s in 20 ms frames */
    CHECK(NEAR(newphrase_tail_kill(&np), 1.f),
          "after 400 s of holding, tail_kill reads %.4f, want 1.0",
          (double)newphrase_tail_kill(&np));
}

/* Same guarantee as above, but forced through the vulnerable path directly: one enormous
   dt_us in a single call, from a held_us a hair below the cap. Under the old clamp-after-add
   code this summed to held_us + dt_us = 2099999 + 4292867297 = 4294967296, exactly 2^32,
   which wraps a 32 bit unsigned to 0 - past the cap by the intent of the check, but the
   stored value is 0, so tail_kill would drop from ~1.0 back to 0.0 in a single tick. The
   accumulation test above never hits this because it only ever adds small deltas. */
static void test_a_single_enormous_tick_saturates_without_wrapping(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, 2099999u);
    newphrase_tick(&np, 4292867297u);
    CHECK(NEAR(newphrase_tail_kill(&np), 1.f),
          "a single huge tick wrapped tail_kill to %.4f, want 1.0",
          (double)newphrase_tail_kill(&np));
}

static void test_release_returns_to_idle(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US + NP_TAIL_FADE_US);
    newphrase_release(&np);
    CHECK(newphrase_held(&np) == 0, "still held after release");
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "still collapsing the tail after release: %.4f", (double)newphrase_tail_kill(&np));
}

/* Ticks arriving while idle must not accumulate, or the next press would start part-way
   through the fade. */
static void test_idle_ticks_do_not_accumulate(void) {
    newphrase_t np = {0, 0};
    newphrase_tick(&np, NP_TAIL_HOLD_US + NP_TAIL_FADE_US);
    newphrase_press(&np);
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "idle ticks leaked into the next press: %.4f", (double)newphrase_tail_kill(&np));
}

/* The planned call site clamps a negative UI delta to exactly 0 rather than skipping the
   call, so a zero-length tick is a real path, not a hypothetical - it must be a true no-op
   and must not disturb a hold already in progress. */
static void test_zero_tick_is_a_no_op(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US / 2u);
    const float before = newphrase_tail_kill(&np);
    newphrase_tick(&np, 0u);
    CHECK(NEAR(newphrase_tail_kill(&np), before),
          "a zero-length tick moved tail_kill from %.4f to %.4f",
          (double)before, (double)newphrase_tail_kill(&np));
    CHECK(newphrase_held(&np) == 1, "a zero-length tick dropped the hold");
}

/* A re-press mid-hold is a fresh contact, not a continuation of the old one, so held_us must
   snap back to 0 rather than carry the running timer forward - pin that it is intended. */
static void test_press_while_active_resets_held_us(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US + NP_TAIL_FADE_US);
    CHECK(NEAR(newphrase_tail_kill(&np), 1.f),
          "setup: expected a fully collapsed tail before the re-press");
    newphrase_press(&np);
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "re-press did not reset held_us: tail_kill reads %.4f, want 0.0",
          (double)newphrase_tail_kill(&np));
    CHECK(newphrase_held(&np) == 1, "re-press dropped the hold");
}

/* The panel calls release on the down-to-up edge without first checking whether it was
   already idle (a stray release, or two in a row), so this must be harmless and idempotent. */
static void test_release_while_idle_is_harmless(void) {
    newphrase_t np = {0, 0};
    newphrase_release(&np);
    CHECK(newphrase_held(&np) == 0, "release while idle set the hold");
    CHECK(NEAR(newphrase_tail_kill(&np), 0.f),
          "release while idle disturbed tail_kill: %.4f", (double)newphrase_tail_kill(&np));
    newphrase_release(&np);
    CHECK(newphrase_held(&np) == 0, "a second release while idle set the hold");
}

int main(void) {
    test_press_stamps_immediately_and_spares_the_tail();
    test_short_hold_never_touches_the_tail();
    test_tail_collapse_is_linear_across_the_fade();
    test_a_very_long_hold_saturates();
    test_a_single_enormous_tick_saturates_without_wrapping();
    test_release_returns_to_idle();
    test_idle_ticks_do_not_accumulate();
    test_zero_tick_is_a_no_op();
    test_press_while_active_resets_held_us();
    test_release_while_idle_is_harmless();
    if (failures) { printf("newphrase_test: %d failure(s)\n", failures); return 1; }
    printf("newphrase_test: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```sh
clang -O2 -std=c11 -Wall -Wextra -Werror ambiotica_port/panel/newphrase_test.c -o /tmp/np_test
```

Expected: FAIL, `'newphrase.h' file not found`.

- [ ] **Step 3: Write the header**

Create `ambiotica_port/panel/newphrase.h`:

```c
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
```

- [ ] **Step 4: Run the test to verify it passes**

```sh
clang -O2 -std=c11 -Wall -Wextra -Werror ambiotica_port/panel/newphrase_test.c -o /tmp/np_test && /tmp/np_test
```

Expected: `newphrase_test: all passed`

- [ ] **Step 5: Wire into tests.sh**

In `ambiotica_port/panel/tests.sh`, change line 11 from:

```sh
for t in stepcond mipmap steptime; do
```

to:

```sh
for t in stepcond mipmap steptime newphrase; do
```

- [ ] **Step 6: Wire into amalgamate.sh**

In `ambiotica_port/panel/amalgamate.sh`, after line 43 (`strip "$HN/stepcond.h"`), add:

```sh
    strip "$HN/newphrase.h"           # x + REC hold timer; inlined ahead of panel.cpp
```

- [ ] **Step 7: Run the full panel test suite**

Run: `sh ambiotica_port/panel/tests.sh`
Expected: `newphrase_test: all passed` among the others, no `FAIL` lines.

- [ ] **Step 8: Commit**

```bash
git add ambiotica_port/panel/newphrase.h ambiotica_port/panel/newphrase_test.c \
        ambiotica_port/panel/tests.sh ambiotica_port/panel/amalgamate.sh
git commit -m "Add the New Phrase hold timer, with tests"
```

---

### Task 2: Repair the looper's refill after a clear - PARKED

**Status: parked, not implemented on this branch.** The fix works and is verified, but
shipping it turned out to also change Event Horizon's release behaviour by an amount nobody
could explain, and the user decided an unexplained hardware behaviour change is worse than
living with the slow bloom-in at LOOP 100% that the instrument already ships. The complete
fix - the `in_g` write-law change, the `since_clear` seed needed to keep it from firing at
power-on, a `looper_is_empty` predicate and a drift-regeneration gate that narrowed but did not
close the unexplained gap, a regression test, and the corrected `np_main.c` measurement window
that finally made the test trustworthy - is preserved on the `refill-fix-investigation` branch,
along with a full writeup at
`docs/superpowers/notes/2026-08-03-looper-refill-investigation.md`. Read that file before
reviving this task; it records what was proven, what was retracted, and the next thing to try
(the Dilate reverse-head read, which shares the same drain cursor and was never isolated).

What DID ship on this branch, because it stands on its own: `ambiotica_port/harness/np_main.c`
exists with the allocator, `run()`, `peak()`, and the chain setup, plus a clipping guard on
`built` (the desktop harness hard-clips at unity since `FC_SOFT_CLIP` is panel-only, and an
over-hot test signal makes any ratio-based assertion read 100% regardless of the code under
test). `ambiotica_port/harness/build.sh` builds and runs it. `ambiotica_port/dsp/looper.c`,
`looper.h`, `microloop.c`, `drain.h`, and `ambiotica_port/harness/full_chain.h` are all back to
their pre-Task-2 state.

---

### Task 3: Tail collapse and the edge declick

**Goal:** Add `tail_kill` and the edge residual to `fc_state`, so a long hold collapses the plate and an instant clear does not click.

**Note: this task no longer inherits Task 2's measurement.** Task 2 is parked (see above), so
`np_main.c` on this branch has no refill assertion to build on top of - only the scaffolding
(allocator, `run()`, `peak()`, chain setup, the clipping guard on `built`). Any code block below
that assumes a `refilled` variable or a clear-then-measure flow already exists needs to
establish that context itself, or add it fresh, rather than editing something already there.

**Files:**
- Modify: `ambiotica_port/harness/full_chain.h:61-75` (the `fc_state` struct)
- Modify: `ambiotica_port/harness/full_chain.h:233-249` (the `rin` loop and the decay)
- Modify: `ambiotica_port/harness/full_chain.h:262-263` (the `wb` sum)
- Modify: `ambiotica_port/harness/np_main.c`

**Acceptance Criteria:**
- [ ] With `tail_kill = 0` the rendered output is bit-identical to before the change
- [ ] Immediately after a clear with `tail_kill = 0`, the plate is still audible (above 0.02) - the tail rings out
- [ ] After holding `tail_kill = 1` for 2 s with no input, output falls below 0.01
- [ ] The block containing a clear has no sample-to-sample jump larger than the pre-clear peak, i.e. the edge is a slope and not a step
- [ ] `sh ambiotica_port/harness/build.sh` passes

**Verify:** `sh ambiotica_port/harness/build.sh` → `PASS` on all four np_main checks, exit 0

**Steps:**

- [ ] **Step 1: Extend the test**

In `ambiotica_port/harness/np_main.c`, add a max-step helper next to `peak`:

```c
/* Largest sample-to-sample jump in a block, carried across the block boundary. A hard cut
   shows up here as a step the size of the signal that was cut; a declicked one does not. */
static float g_prev_l = 0.f, g_prev_r = 0.f;
static float max_step(const float *a, const float *c, int n) {
    float mx = 0.f;
    for (int i = 0; i < n; i++) {
        float dl = fabsf(a[i] - g_prev_l), dr = fabsf(c[i] - g_prev_r);
        if (dl > mx) mx = dl; if (dr > mx) mx = dr;
        g_prev_l = a[i]; g_prev_r = c[i];
    }
    return mx;
}
```

Then replace the body of `main` after the refill check (from `const float WANT` to the end) with:

```c
    const float WANT = 0.80f * built;
    if (refilled < WANT) {
        printf("\nFAIL: refill reached %.4f of %.4f (%.0f%%), want >= 80%%\n",
               refilled, built, (double)(100.f * refilled / built));
        fail = 1;
    } else {
        printf("PASS: refill reaches %.0f%% of the pre-clear level in one pass\n",
               (double)(100.f * refilled / built));
    }

    /* ---- the tail must survive the clear ---- */
    /* This is the feature. It is also the assertion that fails the day someone adds a plate
       reset for tidiness, which is why it is worth its own check rather than an eyeball. */
    run(SECS(4), 1);
    looper_mark_clear(l); microloop_mark_clear(m);
    st.tailKill = 0.f;
    float tail = run(SECS(0.5), 0);      /* input silent: anything heard is the plate */
    printf("half a second after a clear, no input: peak %.4f\n", tail);
    if (tail < 0.02f) {
        printf("\nFAIL: the tail died with the loop (%.4f) - the plate must ring out\n", tail);
        fail = 1;
    } else {
        printf("PASS: the plate is still ringing after the clear\n");
    }

    /* ---- the edge must be a slope, not a step ---- */
    /* Measured against the signal's OWN slew rather than against its peak. Comparing to the
       peak would let a hard cut pass: the cut removes the bed but not the wet, so the step is
       smaller than the full output and a peak-based limit never fires. What a click actually
       is, is a jump far larger than anything the running signal produces, so that is what
       gets measured - baseline first, then the cut. */
    run(SECS(6), 1);
    float baseline = 0.f;
    g_prev_l = outL[BLK-1]; g_prev_r = outR[BLK-1];
    for (int n = 0; n < SECS(0.2); n += BLK) {
        for (int i = 0; i < BLK; i++) {
            float s = (float)(sin(phase) * 0.4); phase += 2.0 * M_PI * 220.0 / SR;
            inL[i] = inR[i] = s;
        }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = max_step(outL, outR, BLK); if (q > baseline) baseline = q;
    }
    looper_mark_clear(l); microloop_mark_clear(m);
    st.cutPending = 1;
    float step = 0.f;
    for (int n = 0; n < SECS(0.05); n += BLK) {
        for (int i = 0; i < BLK; i++) { inL[i] = inR[i] = 0.f; }
        fc_render_block(&st, l, g, m, h, b, d, &p, SR, inL, inR, outL, outR, BLK);
        float q = max_step(outL, outR, BLK); if (q > step) step = q;
    }
    printf("largest jump across the cut:           %.4f (steady play was %.4f)\n", step, baseline);
    if (step > 3.0f * baseline) {
        printf("\nFAIL: the cut jumps %.1fx the signal's own slew - it will click\n",
               (double)(step / baseline));
        fail = 1;
    } else {
        printf("PASS: the cut is smoothed into a slope\n");
    }

    /* ---- a long hold collapses the plate ---- */
    run(SECS(6), 1);
    st.tailKill = 1.f;
    float collapsed = run(SECS(2), 0);
    printf("2 s of full tail_kill, no input:       peak %.4f\n", collapsed);
    if (collapsed > 0.01f) {
        printf("\nFAIL: the plate is still sounding at %.4f after a full collapse\n", collapsed);
        fail = 1;
    } else {
        printf("PASS: a long hold empties the plate\n");
    }
    st.tailKill = 0.f;

    return fail;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `sh ambiotica_port/harness/build.sh`
Expected: compile error, `no member named 'tailKill' in 'fc_state'`.

- [ ] **Step 3: Add the fields to fc_state**

In `ambiotica_port/harness/full_chain.h`, inside the `fc_state` struct, after the `gravPhase` line (line 73), add:

```c
    /* NEW PHRASE (x + REC). Written directly by the panel each block.
     *
     * DELIBERATELY NOT IN full_params. fc_render_block memcmps full_params to gate
     * fc_push_params, so a float moving across a 1200 ms fade would clear that gate on
     * roughly every fourth block and turn a slow visual fade into several hundred
     * powf/expf coefficient re-pushes. Here the gate never sees them. */
    float tailKill;              /* 0..1, applied; the panel one-poles toward its target */
    int   cutPending;            /* set by the panel on the block a clear lands */
    float cutL, cutR;            /* decaying residual that turns the cut into a slope */
    float lastBedL, lastBedR;    /* previous block's final bed sample, to seed the residual */
```

- [ ] **Step 4: Apply tail_kill to the reverb send and the decay**

`tk` is read by two sibling blocks, so it must be declared at function scope, not inside either
of them. In `fc_render_block`, immediately after the `horizonClear` line (line 195), add:

```c
    /* New Phrase's long hold, clamped once and used twice below. The plate cannot be silenced
     * by decay alone (decay is the tank's feedback, not its input gain - see the reverb send
     * comment), so collapsing the tail means cutting what goes IN as well as what recirculates.
     * 0 = untouched, 1 = fully collapsed. */
    const float tk = st->tailKill < 0.f ? 0.f : (st->tailKill > 1.f ? 1.f : st->tailKill);
```

Then replace the `dryToRev` lines 240-242:

```c
      const float dryToRev = p->decay < 0.f ? 0.f : (p->decay > 1.f ? 1.f : p->decay);
      for (int i = 0; i < n; i++) { st->rinL[i] = dryToRev * st->blL[i] + st->layL[i] + microRevSend * st->micL[i];
                                    st->rinR[i] = dryToRev * st->blR[i] + st->layR[i] + microRevSend * st->micR[i]; } }
```

with (the trailing `} }` closes the `for` and the enclosing `{ float mp = ...` block - keep both):

```c
      const float dryToRev = p->decay < 0.f ? 0.f : (p->decay > 1.f ? 1.f : p->decay);
      /* Half of the collapse. Folded into the existing send rather than added as a pass. */
      const float sendG = 1.0f - tk;
      for (int i = 0; i < n; i++) { st->rinL[i] = sendG * (dryToRev * st->blL[i] + st->layL[i] + microRevSend * st->micL[i]);
                                    st->rinR[i] = sendG * (dryToRev * st->blR[i] + st->layR[i] + microRevSend * st->micR[i]); } }
```

Then replace the decay line at 245-246:

```c
    { float t = (p->decay - 0.30f) * (1.0f / 0.70f); if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
      dattorro_set_decay(st->dat, t);                       /* Tail -> tail length (tracks the plugin) */
```

with:

```c
    { float t = (p->decay - 0.30f) * (1.0f / 0.70f); if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
      /* The other half. NOT an output duck: ducking would hide a tail that is still in the
       * tank, and releasing mid-hold would swell it back. Killing the feedback empties the
       * tank for real, so everything heard was real and release can resurrect nothing. */
      t *= (1.0f - tk);
      dattorro_set_decay(st->dat, t);                       /* Tail -> tail length (tracks the plugin) */
```

- [ ] **Step 5: Add the edge residual**

Replace the `wb` sum at lines 262-263:

```c
    for (int i = 0; i < n; i++) { st->wbL[i] = st->layL[i] + st->micL[i] + st->wetL[i];
                                  st->wbR[i] = st->layR[i] + st->micR[i] + st->wetR[i]; }
```

with:

```c
    /* THE EDGE, NOT THE ACTION. Marking the buffer clear takes the bed from full level to zero
     * in one sample, which is a step and therefore a click. Fading the bed out and clearing at
     * the bottom would fix it by DELAYING the clear, and the whole point of the gesture is that
     * it is immediate - so the clear stays on the first available sample and the edge is
     * smoothed instead. The residual is seeded from the last bed sample before the cut and
     * decayed to zero over about 5 ms, added on top of an output that is already silent, so
     * nothing waits on it. Kept out of the reverb send: 5 ms of decaying residual into a
     * diffusion network is inaudible and the output bus alone is simpler. */
    if (st->cutPending) {
        st->cutL = st->lastBedL; st->cutR = st->lastBedR;
        st->cutPending = 0;
    }
    const float cutDecay = 1.0f - (1.0f / (0.0015f * (float)sr));   /* ~1.5 ms one-pole */
    for (int i = 0; i < n; i++) { st->wbL[i] = st->layL[i] + st->micL[i] + st->wetL[i] + st->cutL;
                                  st->wbR[i] = st->layR[i] + st->micR[i] + st->wetR[i] + st->cutR;
                                  st->cutL *= cutDecay; st->cutR *= cutDecay; }
    if (n > 0) { st->lastBedL = st->layL[n-1] + st->micL[n-1];
                 st->lastBedR = st->layR[n-1] + st->micR[n-1]; }
```

- [ ] **Step 6: Run it to verify it passes**

Run: `sh ambiotica_port/harness/build.sh`
Expected: four `PASS` lines from `amb_np_test` (refill, tail survives, cut is a slope, long hold empties the plate), exit 0. `amb_eh_test` must still pass.

- [ ] **Step 7: Confirm the no-op case is untouched**

The float harness renders a fixed program to `out_i16.wav`. With `tailKill` and `cutPending` left at zero the chain must be unchanged:

```sh
git stash && sh ambiotica_port/harness/build.sh && cp out_i16.wav /tmp/before.wav
git stash pop && sh ambiotica_port/harness/build.sh && cmp /tmp/before.wav out_i16.wav
```

Expected: `cmp` is silent. If it differs, the residual or the send gain is being applied when it should be inert - the likely cause is `sendG` not being exactly 1.0 at `tailKill == 0`.

- [ ] **Step 8: Commit**

```bash
git add ambiotica_port/harness/full_chain.h ambiotica_port/harness/np_main.c
git commit -m "Collapse the plate on a long hold, and smooth the clear's edge"
```

---

### Task 4: Wire the gesture to the REC pad

**Goal:** `× + REC` drives the clear, the hold and the tail collapse, with the REC pad showing what is happening.

**Files:**
- Modify: `ambiotica_port/panel/panel.cpp:96-111` (the control-pad enum)
- Modify: `ambiotica_port/panel/panel.cpp:236-245` (panel state)
- Modify: `ambiotica_port/panel/panel.cpp:707-718` (`draw_nav`, where `×` is read)
- Modify: `ambiotica_port/panel/panel.cpp:1373-1374` (`on_ui`, beside `tick_kit_arm`)
- Modify: `ambiotica_port/panel/panel.cpp:1640-1674` (`on_dsp`, beside the Event Horizon flush)

**Acceptance Criteria:**
- [ ] `COL_REC = 12` is defined in the `CTL_DN` group with the manual quote as its comment
- [ ] The REC pad is dark unless `×` is held, on every page
- [ ] Holding `×` and pressing REC calls `looper_mark_clear` and `microloop_mark_clear` every block until release
- [ ] Past the threshold the pad's brightness tracks `1 - tail_kill`, reaching black as the tail goes
- [ ] `×` + REC never falls through to a page navigation
- [ ] `sh ambiotica_port/panel/amalgamate.sh` reports no order problem
- [ ] `sh ambiotica_port/panel/tests.sh` passes, including `check_order.py` on the generated file

**Verify:** `sh ambiotica_port/panel/amalgamate.sh && sh ambiotica_port/panel/tests.sh` → `wrote .../plinky_ambiotica.cpp`, no `ORDER PROBLEM`, no `FAIL`

**Steps:**

- [ ] **Step 1: Add the pad constant**

In `ambiotica_port/panel/panel.cpp`, in the enum, immediately before `COL_X = 13` (line 107), add:

```c
    /* "REC" (row 15, col 12) - the printed record circle, immediately left of x. New Phrase:
       x + REC empties the loop and micro-loop and leaves the plate ringing. This is stock
       Chords grammar rather than a repurpose - the manual says x "lets you reset or delete
       individual things across all of Plinky 12 Chords if you hold it and tap one of the
       other pads", and the MIDI chord flow says "you can tap the X pad to wipe your
       recording". On this panel the rolling looper IS the recording.
       Dark unless x is held: we have no sequencer to record into, so the pad has no function
       of its own. NB stock Chords also uses Rec alone as the RANDOMISE modifier; we put
       randomise on FILL and claim only the x combination, so nothing collides. */
    COL_REC    = 12,
```

- [ ] **Step 2: Add the panel state**

Near the other UI state around line 242 (beside `eh_flushed`), add:

```c
    newphrase_t    newphrase = {0, 0};   /* x + REC hold timer; see newphrase.h */
    float          np_tail_sm = 0.f;     /* smoothed tail_kill, pushed to fc_state each block */
    bool           np_was_down = false;  /* edge detect, so the residual is seeded once */
```

- [ ] **Step 3: Detect the gesture in draw_nav**

In `draw_nav`, after the `×` LED line (line 711, `set_led(COL_X, page_y + CTL_DN, ...)`), add:

```c
        /* NEW PHRASE - x + REC. Read as a widget so the press edge is clean, but only while x
           is held; with x up the pad is dark and inert, because a panel with no sequencer has
           nothing to record. The whole gesture is on the bottom-right corner, two adjacent
           pads, so it is a one-handed move. */
        {
            const float tk = newphrase_tail_kill(&newphrase);
            uint32_t rc = 0;
            if (xh) {
                /* Past the threshold the pad darkens in step with the tail it is collapsing,
                   reaching black exactly as the tail goes. The timing is the only part of this
                   gesture you cannot feel, so it is the part that has to be visible. */
                rc = newphrase_held(&newphrase) ? fade_col(RED, (int)(255.f * (1.f - tk)))
                                                 : DIMMER(RED);
            }
            set_led(COL_REC, page_y + CTL_DN, rc);
            const bool rec_down = xh && get_touch_down(COL_REC, page_y + CTL_DN) != 0;
            if (rec_down && !newphrase_held(&newphrase)) newphrase_press(&newphrase);
            else if (!rec_down && newphrase_held(&newphrase)) newphrase_release(&newphrase);
            /* Two calls, not a ternary into the format argument - set_help_text is a printf
               style function and a non-literal format is a warning waiting to happen. */
            if (newphrase_held(&newphrase)) {
                if (tk > 0.f) set_help_text("New phrase - clearing the tail too");
                else          set_help_text("New phrase - clear the loop, keep the tail");
            }
        }
```

- [ ] **Step 4: Tick the timer on the UI thread**

In `on_ui`, beside `tick_kit_arm(dt_us);` (line 1374), add:

```c
        newphrase_tick(&newphrase, (unsigned)(dt_us < 0 ? 0 : dt_us));
```

- [ ] **Step 5: Drive the DSP from on_dsp**

In `on_dsp`, immediately before the Event Horizon block at line 1640 (`if (fx_sm.horizon < 0.04f) {`), add:

```c
        /* NEW PHRASE. Re-stamped every block for as long as the pad is down, exactly like the
           Event Horizon flush below and for the same reason: holding then means "stay empty",
           and releasing starts recording from empty. The plate, Spectra, bloom and drift are
           all left alone, so the tail rings out - that is the entire difference between this
           gesture and Event Horizon. */
        {
            const bool np_down = newphrase_held(&newphrase) != 0;
            if (np_down) {
                if (!np_was_down) st.cutPending = 1;   /* seed the edge residual, once */
                if (looper)    looper_mark_clear(looper);
                if (microloop) microloop_mark_clear(microloop);
            }
            np_was_down = np_down;
            /* One-pole toward the target, ~50 ms, so releasing mid-fade returns the reverb
               send to unity smoothly. Negligible lag against a 1200 ms ramp. */
            const float tgt = newphrase_tail_kill(&newphrase);
            np_tail_sm += 0.04f * (tgt - np_tail_sm);
            if (np_tail_sm < 5e-4f && tgt == 0.f) np_tail_sm = 0.f;
            st.tailKill = np_tail_sm;
        }
```

`np_was_down` was declared in Step 2. `st` is the `fc_state` already in scope here, the same
one passed to `fc_render_block` further down this function.

- [ ] **Step 6: Amalgamate and check the order**

Run: `sh ambiotica_port/panel/amalgamate.sh`
Expected: `wrote .../plinky_ambiotica.cpp (... lines, ... bytes)` and no `ORDER PROBLEM` line. `newphrase.h` must appear before `panel.cpp` in the output:

```sh
grep -n "AMBIOTICA_NEWPHRASE_H\|newphrase_tail_kill" ambiotica_port/panel/plinky_ambiotica.cpp | head
```

Expected: the `#ifndef`-guarded definition appears at a lower line number than its first use.

- [ ] **Step 7: Run the tests**

Run: `sh ambiotica_port/panel/tests.sh`
Expected: no `FAIL` lines.

- [ ] **Step 8: Commit**

```bash
git add ambiotica_port/panel/panel.cpp ambiotica_port/panel/plinky_ambiotica.cpp \
        ambiotica_port/panel/plinky_ambiotica_profile.cpp ambiotica_port/panel/dist
git commit -m "New Phrase: hold x + REC to clear the loop and keep the tail"
```

---

### Task 5: Confirm on hardware

**Goal:** Prove the gesture behaves on the device, and that it costs nothing measurable, since the desktop harness cannot see either.

**Files:**
- Modify: none expected. Any fix found here is a follow-up commit.

**Acceptance Criteria:**
- [ ] `× + REC` clears the loop with no audible click, and the reverb tail continues
- [ ] The gesture works from the play page and from the drums page
- [ ] A ~2 s hold takes the plate down to silence
- [ ] The REC pad is dark with `×` up, and visibly darkens during the long hold
- [ ] `×` does not drop when REC is pressed (the unconfirmed `× + PRESET` symptom)
- [ ] The `_profile` build's `STG` line shows no new time in the loop, micro or reverb stages

**Verify:** flash the profile build, watch the `STG` line in Device Logs on plinky12.com, and play the gesture

**Steps:**

- [ ] **Step 1: Build both variants**

```sh
sh ambiotica_port/panel/amalgamate.sh
sh ambiotica_port/harness/build.sh
sh ambiotica_port/panel/tests.sh
```

Expected: all three clean.

- [ ] **Step 2: Push and get a dist-pinned URL**

```bash
git push
```

CI publishes to the `dist` branch. Hand the user a **dist-SHA-pinned** URL, never a branch URL, because the next force-push invalidates branch URLs:

```sh
git ls-remote origin dist
```

Then build the link as
`https://raw.githubusercontent.com/charlesvestal/plinky-ambiotica/<dist-sha>/plinky_ambiotica_profile.cpp`

- [ ] **Step 3: Ask the user to flash and play**

Flash from **plinky12.com**, not stage: `printf` and Device Logs do not work on staging, and the `STG` line is the whole point of this step.

Expect the WebUSB log to die on panel load. That is a known firmware bug, reproducible on stock Chords, and the instrument is unaffected. Do not re-debug it as a panel hang.

Ask the user to report:
1. Does a stab on `× + REC` clear the loop, with the reverb continuing?
2. Any click at the moment of the clear?
3. Does about 2 s of holding take it to silence?
4. Does the REC pad darken as the tail goes?
5. Does it work on the drums page too?

- [ ] **Step 4: Read the STG line**

The chain sits around 1160 µs of a 2000 µs block. Compare the `STG` numbers for the loop, micro and reverb stages against that baseline, with the gesture idle and while holding it.

Expected: no measurable change. The additions are one multiply on the send, one on the decay per block, and a multiply-add for 5 ms after a cut.

If a stage has moved, change one thing at a time. The desktop harness cannot measure CPU, so this is the only signal.

- [ ] **Step 5: Check the arena**

Watch for the `PANEL: sizeof=.. free=.. dsp_ok=..` print. The recorded figures for free arena disagree with each other, so read the print rather than trusting either. This task adds a handful of bytes of panel state plus five floats in `fc_state`.

- [ ] **Step 6: Commit any fixes and update the memory**

If hardware disagrees with the design, fix it and note what was wrong. Either way, update `plinky-ambiotica-open-items` with the gesture's shipped state, and resolve the `× + PRESET` UNCONFIRMED note in whichever direction this gesture settles it.

---

## Notes for the implementer

- **`panel.cpp` alone does not compile.** It has no `#include`s and only type-checks after amalgamation. Ignore clang "unknown type name looper_t" noise from an IDE. The real check is `amalgamate.sh` plus `check_order.py`.
- **Never loop over `fc_render`** in a test. It calls `fc_init`, a memset of the whole state including the dattorro pointer, so a per-block loop over it rebuilds the chain every call and the test passes vacuously. Use `fc_render_block` with your own persistent `fc_state`, as `eh_main.c` and `np_main.c` do.
- **No em dashes** in comments, docs or commit messages.
- The generated `.cpp` files are comment-stripped on purpose; the flash endpoint rejects uploads over about 200 KB. Keep every word in the sources.
