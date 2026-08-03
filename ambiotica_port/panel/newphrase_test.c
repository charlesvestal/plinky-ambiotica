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
    CHECK(newphrase_stamp(&np) == 1, "press did not ask for a stamp");
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
    CHECK(newphrase_stamp(&np) == 1, "stamp dropped while still held");
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

static void test_release_returns_to_idle(void) {
    newphrase_t np = {0, 0};
    newphrase_press(&np);
    newphrase_tick(&np, NP_TAIL_HOLD_US + NP_TAIL_FADE_US);
    newphrase_release(&np);
    CHECK(newphrase_stamp(&np) == 0, "still stamping after release");
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

int main(void) {
    test_press_stamps_immediately_and_spares_the_tail();
    test_short_hold_never_touches_the_tail();
    test_tail_collapse_is_linear_across_the_fade();
    test_a_very_long_hold_saturates();
    test_release_returns_to_idle();
    test_idle_ticks_do_not_accumulate();
    if (failures) { printf("newphrase_test: %d failure(s)\n", failures); return 1; }
    printf("newphrase_test: all passed\n");
    return 0;
}
