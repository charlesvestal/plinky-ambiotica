/* Unit tests for the two per-step CONDITION controls, which are independent of each other
   exactly as they are on stock Chords: PROB is a chance percentage, MODULO is "play only the
   n-th time out of N loops". See stepcond.h. Run via tests.sh. */
#include <stdio.h>
#include <string.h>

#include "stepcond.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static const char* kProbRing[] = { "100", "75", "50", "25" };
#define PROB_N ((int)(sizeof kProbRing / sizeof kProbRing[0]))

static const char* kModRing[] = {
    "--", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
};
#define MOD_RING_N ((int)(sizeof kModRing / sizeof kModRing[0]))

/* PROB keeps the short ring it always had — adding modulo must not lengthen it. */
static void test_prob_ring_is_a_closed_cycle(void) {
    unsigned char p = 127;
    for (int i = 0; i < PROB_N; i++) {
        CHECK(strcmp(prob_label(p), kProbRing[i]) == 0,
              "prob position %d reads [%s], want [%s]", i, prob_label(p), kProbRing[i]);
        CHECK(p != 0, "prob position %d turned the step off", i);
        p = next_prob(p);
    }
    CHECK(p == 127, "prob ring did not close: p=%d after %d taps, want 127", p, PROB_N);
}

/* MODULO's ring includes "off", so a step can be given a condition and then taken back to
   plain without having to erase and redraw it. */
static void test_mod_ring_is_a_closed_cycle(void) {
    unsigned char m = 0;
    for (int i = 0; i < MOD_RING_N; i++) {
        CHECK(strcmp(mod_label(m), kModRing[i]) == 0,
              "mod position %d reads [%s], want [%s]", i, mod_label(m), kModRing[i]);
        m = next_mod(m);
    }
    CHECK(m == 0, "mod ring did not close: m=%d after %d taps, want 0", m, MOD_RING_N);
}

/* Four characters is all that fits across 16 columns in FONT_4. */
static void test_labels_fit_the_display(void) {
    unsigned char p = 127;
    for (int i = 0; i < PROB_N; i++, p = next_prob(p))
        CHECK(strlen(prob_label(p)) <= 4, "prob label [%s] too wide", prob_label(p));
    unsigned char m = 0;
    for (int i = 0; i < MOD_RING_N; i++, m = next_mod(m))
        CHECK(strlen(mod_label(m)) <= 4, "mod label [%s] too wide", mod_label(m));
}

static void test_off_step_never_fires(void) {
    for (unsigned char m = 0; m <= (unsigned char)COND_N; m++)
        for (unsigned int pass = 0; pass < 8; pass++)
            CHECK(!step_fires(0, m, pass, 0), "off step fired with mod=%d pass=%u", m, pass);
}

/* With no modulo set, behaviour must be byte-for-byte what it was before this feature —
   that is what keeps every existing saved scene sounding the same. */
static void test_no_modulo_is_pure_probability(void) {
    for (unsigned int pass = 0; pass < 8; pass++) {
        for (unsigned int roll = 0; roll < 127; roll++) {
            CHECK(step_fires(127, 0, pass, roll), "100%% missed at pass=%u roll=%u", pass, roll);
            CHECK(step_fires(32, 0, pass, roll) == (roll % 127u < 32),
                  "25%% wrong at roll=%u", roll);
        }
    }
}

static void test_probability_boundary(void) {
    int fired = 0;
    for (unsigned int roll = 0; roll < 127; roll++) if (step_fires(32, 0, 0, roll)) fired++;
    CHECK(fired == 32, "25%% step fired on %d of 127 rolls, want 32", fired);
    CHECK(step_fires(32, 0, 0, 31), "roll 31 should fire against 32");
    CHECK(!step_fires(32, 0, 0, 32), "roll 32 should miss against 32");
}

/* "Play only the n-th time out of N loops": 1:2 and 2:2 must interleave exactly. */
static void test_modulo_alternates(void) {
    CHECK(strcmp(mod_label(1), "1:2") == 0, "expected 1:2 at mod index 1");
    CHECK(strcmp(mod_label(2), "2:2") == 0, "expected 2:2 at mod index 2");
    for (unsigned int pass = 0; pass < 8; pass++) {
        int a = step_fires(127, 1, pass, 0), b = step_fires(127, 2, pass, 0);
        CHECK(a != b, "1:2 and 2:2 both %s on pass %u", a ? "fired" : "stayed silent", pass);
        CHECK(a == (pass % 2 == 0), "1:2 wrong on pass %u", pass);
    }
}

/* Every condition fires exactly once per B passes across several cycles, so a phase error
   or a drifting pass counter shows up rather than averaging out. */
static void test_each_condition_fires_once_per_cycle(void) {
    for (int i = 1; i <= COND_N; i++) {
        int b = cond_of(i - 1)->b;
        for (int cycle = 0; cycle < 4; cycle++) {
            int fired = 0;
            for (int k = 0; k < b; k++)
                if (step_fires(127, (unsigned char)i, (unsigned int)(cycle * b + k), 0)) fired++;
            CHECK(fired == 1, "%s fired %d times in cycle %d of %d passes",
                  mod_label((unsigned char)i), fired, cycle, b);
        }
    }
}

/* Modulo is deterministic — it must ignore the dice completely. */
static void test_modulo_ignores_the_roll(void) {
    for (int i = 1; i <= COND_N; i++)
        for (unsigned int pass = 0; pass < 12; pass++) {
            int want = step_fires(127, (unsigned char)i, pass, 0);
            for (unsigned int roll = 1; roll < 127; roll += 13)
                CHECK(step_fires(127, (unsigned char)i, pass, roll) == want,
                      "%s changed with the roll at pass %u", mod_label((unsigned char)i), pass);
        }
}

/* The two conditions are independent and both must pass. A 25% step at 1:2 can only ever
   fire on even passes, and never fires on odd ones however the dice land. */
static void test_conditions_compose(void) {
    for (unsigned int pass = 0; pass < 8; pass++)
        for (unsigned int roll = 0; roll < 127; roll++) {
            int got = step_fires(32, 1, pass, roll);
            int want = (pass % 2 == 0) && (roll % 127u < 32);
            CHECK(got == want, "25%% + 1:2 wrong at pass=%u roll=%u", pass, roll);
        }
}

int main(void) {
    test_prob_ring_is_a_closed_cycle();
    test_mod_ring_is_a_closed_cycle();
    test_labels_fit_the_display();
    test_off_step_never_fires();
    test_no_modulo_is_pure_probability();
    test_probability_boundary();
    test_modulo_alternates();
    test_each_condition_fires_once_per_cycle();
    test_modulo_ignores_the_roll();
    test_conditions_compose();
    if (failures) { printf("\n%d assertion(s) failed\n", failures); return 1; }
    printf("stepcond: all tests passed\n");
    return 0;
}
