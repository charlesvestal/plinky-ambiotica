/* Unit tests for mip addressing.
 *
 * The expected values here are NOT derived from the implementation — they were printed by
 * the on-device probe (report_mips, profile build) on 2026-07-26 while a DivBeats break was
 * loaded, and pasted in verbatim. So this test pins the maths to observed hardware behaviour
 * rather than to my reading of the SDK header. See tests.sh.
 */
#include <stdio.h>

#include "../dsp/mipmap.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* Verbatim from the device log:
     MIP sample_data_va=8421376 tape=1835008 slice=[1251556,1365334) len=113778 */
#define DEV_BASE 8421376u
#define DEV_TAPE 1835008u
#define DEV_OFF  1251556u
#define DEV_LEN  113778u

static const struct { unsigned int base, va, len; } kDevice[] = {
    /* mip0 */ { 8421376u,  9672932u,  113778u },
    /* mip1 */ { 10256384u, 10882162u, 56889u  },
    /* mip2 */ { 11173888u, 11486777u, 28444u  },
    /* mip3 */ { 11632640u, 11789084u, 14222u  },
    /* mip4 */ { 11862016u, 11940238u, 7111u   },
    /* mip5 */ { 11976704u, 12015815u, 3555u   },
    /* mip6 */ { 12034048u, 12053603u, 1777u   },
    /* mip7 */ { 12062720u, 12072497u, 888u    },
    /* mip8: the log was truncated mid-line, so this row is derived, not observed. */
    /* mip8 */ { 12077056u, 12081944u, 444u    },
};

static void test_bases_match_the_device(void) {
    for (unsigned m = 0; m <= 8; m++)
        CHECK(mip_base(DEV_BASE, DEV_TAPE, m) == kDevice[m].base,
              "mip%u base = %u, device printed %u",
              m, mip_base(DEV_BASE, DEV_TAPE, m), kDevice[m].base);
}

static void test_slice_addresses_match_the_device(void) {
    for (unsigned m = 0; m <= 8; m++)
        CHECK(mip_va(DEV_BASE, DEV_TAPE, DEV_OFF, m) == kDevice[m].va,
              "mip%u va = %u, device printed %u",
              m, mip_va(DEV_BASE, DEV_TAPE, DEV_OFF, m), kDevice[m].va);
}

static void test_lengths_match_the_device(void) {
    for (unsigned m = 0; m <= 8; m++)
        CHECK(mip_len(DEV_LEN, m) == kDevice[m].len,
              "mip%u len = %u, device printed %u", m, mip_len(DEV_LEN, m), kDevice[m].len);
}

/* mip 0 must be the untouched original, which is what makes today's playback correct. */
static void test_mip0_is_the_original(void) {
    CHECK(mip_base(DEV_BASE, DEV_TAPE, 0) == DEV_BASE, "mip0 base must be sample_data_va");
    CHECK(mip_va(DEV_BASE, DEV_TAPE, DEV_OFF, 0) == DEV_BASE + DEV_OFF,
          "mip0 va must be sample_data_va + offset");
}

/* Each mip is the previous decimated by two: consecutive bases differ by tape/2^m, and the
   whole pyramid fits in 2*tape — which is what TAPE_LENGTH_SAMPLES "including mips" means. */
static void test_pyramid_halves_and_fits(void) {
    for (unsigned m = 0; m < 8; m++) {
        unsigned int step = mip_base(DEV_BASE, DEV_TAPE, m + 1) - mip_base(DEV_BASE, DEV_TAPE, m);
        CHECK(step == DEV_TAPE >> m, "mip%u..%u gap = %u, want %u", m, m + 1, step, DEV_TAPE >> m);
    }
    CHECK(mip_base(DEV_BASE, DEV_TAPE, 8) - DEV_BASE < 2u * DEV_TAPE,
          "pyramid must fit inside 2x the tape length");
}

/* Overflow guard. get_mip_va does (tape * mip_offset) >> 8 in 32 bits; at the largest legal
   tape (TAPE_LENGTH_SAMPLES = 2<<21) and the largest offset (510) that is 2139095040, which
   fits — but only just, so assert it rather than trusting it stays true. */
static void test_no_overflow_at_max_tape(void) {
    unsigned int max_tape = 2u << 21;
    unsigned int top = mip_base(0, max_tape, 8);
    CHECK(top == ((max_tape * 510u) >> 8), "mip8 of the largest tape overflowed: %u", top);
    CHECK(top < 2u * max_tape, "mip8 base must stay inside the pyramid");
}

/* Which mip to read when pitching up. Pitching DOWN never aliases, so it stays on mip 0. */
static void test_mip_for_rate(void) {
    CHECK(mip_for_rate(0.25f) == 0, "pitching down must use mip 0");
    CHECK(mip_for_rate(0.99f) == 0, "below unity must use mip 0");
    CHECK(mip_for_rate(1.0f)  == 0, "unity must use mip 0");
    CHECK(mip_for_rate(1.99f) == 0, "just under an octave up must still use mip 0");
    CHECK(mip_for_rate(2.0f)  == 1, "an octave up must use mip 1");
    CHECK(mip_for_rate(3.9f)  == 1, "just under two octaves must use mip 1");
    CHECK(mip_for_rate(4.0f)  == 2, "two octaves up must use mip 2");
    CHECK(mip_for_rate(256.f) == 8, "eight octaves up must use mip 8");
    CHECK(mip_for_rate(1e9f)  == 8, "absurd rates must clamp to mip 8, not run off the end");
}

/* +24 semitones is the panel's transpose limit, so this is the deepest mip reachable in
   practice — worth pinning so a change to the clamp is visible here. */
static void test_panel_transpose_range(void) {
    CHECK(mip_for_rate(4.0f) == 2, "+24 semitones (rate 4) should land on mip 2");
    CHECK(mip_len(DEV_LEN, 2) == 28444u, "a break at +24 should read 28444 samples");
}

int main(void) {
    test_bases_match_the_device();
    test_slice_addresses_match_the_device();
    test_lengths_match_the_device();
    test_mip0_is_the_original();
    test_pyramid_halves_and_fits();
    test_no_overflow_at_max_tape();
    test_mip_for_rate();
    test_panel_transpose_range();
    if (failures) { printf("\n%d assertion(s) failed\n", failures); return 1; }
    printf("mipmap: all tests passed\n");
    return 0;
}
