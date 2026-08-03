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
