/* Unit test for euclid_fill.
 *
 * panel.cpp cannot be compiled by the desktop harness (harness/build.sh compiles the DSP
 * sources plus main.c only) and does not compile standalone — it has no #includes and only
 * type-checks after amalgamation. So the generator lives in its own header, which the
 * amalgamation inlines ahead of panel.cpp and this test includes directly.
 *
 *   sh ambiotica_port/panel/euclid_test.sh
 */
#include <stdio.h>
#include <string.h>

#include "../dsp/drums.h"
#include "euclid.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static int count_pulses(const unsigned char* p) {
    int n = 0;
    for (int i = 0; i < DRUM_STEPS; i++) if (p[i]) n++;
    return n;
}

/* Renders a track as "x..x..x" for readable failure output. */
static const char* show(const unsigned char* p) {
    static char buf[DRUM_STEPS + 1];
    for (int i = 0; i < DRUM_STEPS; i++) buf[i] = p[i] ? 'x' : '.';
    buf[DRUM_STEPS] = 0;
    return buf;
}

/* Every k in 1..16 yields exactly k pulses.
   Asserted for all 16 rather than spot-checked: k=6 and k=10 share a factor with 16, which
   is where a naive generator drifts. It holds because as i runs over Z16, (i*k) mod 16 hits
   each multiple of g=gcd(k,16) exactly g times, and k/g of those fall below k. */
static void test_pulse_count_is_exactly_k(void) {
    unsigned char t[DRUM_STEPS];
    for (int k = 1; k <= DRUM_STEPS; k++) {
        euclid_fill(t, k, 0);
        CHECK(count_pulses(t) == k, "k=%d gave %d pulses, want %d  [%s]",
              k, count_pulses(t), k, show(t));
    }
}

/* Rotation is a pure shift, so it must wrap at DRUM_STEPS. */
static void test_rotation_wraps(void) {
    unsigned char a[DRUM_STEPS], b[DRUM_STEPS];
    for (int k = 1; k <= DRUM_STEPS; k++)
        for (int rot = 0; rot < DRUM_STEPS; rot++) {
            euclid_fill(a, k, rot);
            euclid_fill(b, k, rot + DRUM_STEPS);
            CHECK(memcmp(a, b, DRUM_STEPS) == 0,
                  "k=%d rot=%d differs from rot=%d  [%s vs %s]",
                  k, rot, rot + DRUM_STEPS, show(a), show(b));
        }
}

/* Rotation must not change how many pulses there are. */
static void test_rotation_preserves_pulse_count(void) {
    unsigned char t[DRUM_STEPS];
    for (int k = 1; k <= DRUM_STEPS; k++)
        for (int rot = 0; rot < DRUM_STEPS; rot++) {
            euclid_fill(t, k, rot);
            CHECK(count_pulses(t) == k, "k=%d rot=%d gave %d pulses  [%s]",
                  k, rot, count_pulses(t), show(t));
        }
}

static void test_extremes(void) {
    unsigned char t[DRUM_STEPS];
    euclid_fill(t, DRUM_STEPS, 0);
    CHECK(count_pulses(t) == DRUM_STEPS, "k=16 should fill every step  [%s]", show(t));
    euclid_fill(t, 1, 0);
    CHECK(count_pulses(t) == 1, "k=1 should give one pulse  [%s]", show(t));
    CHECK(t[0] != 0, "k=1 rot=0 should put its pulse on step 0  [%s]", show(t));
}

/* The known case from the design doc: E(5,16) at rot 0, and the same shifted right by 2. */
static void test_known_pattern_and_shift_direction(void) {
    unsigned char t[DRUM_STEPS];
    euclid_fill(t, 5, 0);
    CHECK(strcmp(show(t), "x...x..x..x..x..") == 0,
          "E(5,16) rot=0 was [%s], want [x...x..x..x..x..]", show(t));
    euclid_fill(t, 5, 2);
    CHECK(strcmp(show(t), "..x...x..x..x..x") == 0,
          "E(5,16) rot=2 was [%s], want [..x...x..x..x..x]  (positive rot moves right)",
          show(t));
}

/* Pulses carry 127 — "always fires" — matching what hand-painting writes in panel.cpp.
   Rests must be exactly 0, since fire_drum_step treats any non-zero byte as a hit. */
static void test_pulse_values(void) {
    unsigned char t[DRUM_STEPS];
    euclid_fill(t, 5, 0);
    for (int i = 0; i < DRUM_STEPS; i++)
        CHECK(t[i] == 0 || t[i] == 127, "step %d was %d, want 0 or 127", i, t[i]);
}

/* Generation must fully replace the track, leaving nothing of what was there. */
static void test_overwrites_previous_contents(void) {
    unsigned char t[DRUM_STEPS];
    memset(t, 99, sizeof t);
    euclid_fill(t, 4, 0);
    CHECK(count_pulses(t) == 4, "stale bytes survived generation  [%s]", show(t));
    for (int i = 0; i < DRUM_STEPS; i++)
        CHECK(t[i] == 0 || t[i] == 127, "step %d kept stale value %d", i, t[i]);
}

int main(void) {
    test_pulse_count_is_exactly_k();
    test_rotation_wraps();
    test_rotation_preserves_pulse_count();
    test_extremes();
    test_known_pattern_and_shift_direction();
    test_pulse_values();
    test_overwrites_previous_contents();
    if (failures) { printf("\n%d assertion(s) failed\n", failures); return 1; }
    printf("euclid: all tests passed\n");
    return 0;
}
