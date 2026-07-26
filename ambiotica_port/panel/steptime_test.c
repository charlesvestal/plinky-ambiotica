/* Unit tests for per-track step timing: where each track's read head is, which pass of its
   own loop it is on, and how that feeds MODULO. See stepcond.h. Run via tests.sh. */
#include <stdio.h>

#include "../dsp/drums.h"
#include "stepcond.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* A zero length would divide by zero, and a scene saved before per-track length existed has
   no "dlen" field at all — so anything out of range must read as the full 16. */
static void test_length_is_clamped(void) {
    CHECK(track_len(0)  == DRUM_STEPS, "length 0 must default to %d", DRUM_STEPS);
    CHECK(track_len(17) == DRUM_STEPS, "length 17 must clamp to %d", DRUM_STEPS);
    CHECK(track_len(255) == DRUM_STEPS, "garbage must clamp to %d", DRUM_STEPS);
    CHECK(track_len(1)  == 1,  "length 1 is legal");
    CHECK(track_len(7)  == 7,  "length 7 is legal");
    CHECK(track_len(16) == 16, "length 16 is legal");
}

static void test_step_cycles_within_length(void) {
    for (unsigned len = 1; len <= DRUM_STEPS; len++)
        for (unsigned tick = 0; tick < len * 4u; tick++) {
            unsigned s = step_of(tick, (unsigned char)len);
            CHECK(s < len, "len=%u tick=%u gave step %u, out of range", len, tick, s);
            CHECK(s == tick % len, "len=%u tick=%u gave step %u, want %u", len, tick, s, tick % len);
        }
}

/* A track's pass is a loop of ITS OWN length — that is what makes modulo mean "every N times
   round this track" rather than "every N times round a bar it may not share". */
static void test_pass_counts_the_tracks_own_loop(void) {
    for (unsigned tick = 0; tick < 7; tick++)
        CHECK(pass_of(tick, 7) == 0, "tick %u of a 7-step track is still pass 0", tick);
    for (unsigned tick = 7; tick < 14; tick++)
        CHECK(pass_of(tick, 7) == 1, "tick %u of a 7-step track is pass 1", tick);
    CHECK(pass_of(14, 7) == 2, "tick 14 of a 7-step track is pass 2");
    CHECK(pass_of(0, 16) == 0 && pass_of(15, 16) == 0 && pass_of(16, 16) == 1,
          "a 16-step track still counts passes per bar");
}

/* The whole point of the feature: unequal lengths drift apart and only realign at their
   least common multiple. 16 against 7 is 112 ticks — seven bars. */
static void test_polyrhythm_realigns_at_lcm(void) {
    unsigned realign = 0;
    for (unsigned tick = 1; tick < 1000; tick++)
        if (step_of(tick, 16) == 0 && step_of(tick, 7) == 0) { realign = tick; break; }
    CHECK(realign == 112, "16 against 7 should realign at tick 112, got %u", realign);

    unsigned r2 = 0;
    for (unsigned tick = 1; tick < 1000; tick++)
        if (step_of(tick, 16) == 0 && step_of(tick, 5) == 0) { r2 = tick; break; }
    CHECK(r2 == 80, "16 against 5 should realign at tick 80, got %u", r2);
}

/* Length and modulo compose: a 1:2 condition on a 7-step track fires through its first seven
   ticks, stays silent through the next seven, and so on — following the track, not the bar. */
static void test_modulo_follows_the_tracks_own_pass(void) {
    for (unsigned tick = 0; tick < 28; tick++) {
        unsigned pass = pass_of(tick, 7);
        int got  = step_fires(127, 1 /* 1:2 */, pass, 0);
        int want = (tick / 7) % 2 == 0;
        CHECK(got == want, "1:2 on a 7-step track wrong at tick %u (pass %u)", tick, pass);
    }
}

/* Shortening a track must not disturb the others: each reads its own length independently. */
static void test_tracks_are_independent(void) {
    unsigned char len[3] = { 16, 7, 5 };
    for (unsigned tick = 0; tick < 50; tick++) {
        CHECK(step_of(tick, len[0]) == tick % 16, "track 0 disturbed at tick %u", tick);
        CHECK(step_of(tick, len[1]) == tick % 7,  "track 1 disturbed at tick %u", tick);
        CHECK(step_of(tick, len[2]) == tick % 5,  "track 2 disturbed at tick %u", tick);
    }
}

int main(void) {
    test_length_is_clamped();
    test_step_cycles_within_length();
    test_pass_counts_the_tracks_own_loop();
    test_polyrhythm_realigns_at_lcm();
    test_modulo_follows_the_tracks_own_pass();
    test_tracks_are_independent();
    if (failures) { printf("\n%d assertion(s) failed\n", failures); return 1; }
    printf("steptime: all tests passed\n");
    return 0;
}
