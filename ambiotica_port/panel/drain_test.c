/* Event Horizon drain: does the loop actually go silent while the slider is held down?
 *
 * Models looper.c's ring exactly - write head advancing one slot per sample writing silence
 * (feedback is pulled to zero during a drain), read head trailing it by loop_len. The buffer
 * starts full of audio. The only measure that matters is what comes OUT of the read head, so
 * that is what this counts: samples of surviving audio the player would hear, per 100 ms.
 */
#include <stdio.h>
#include <string.h>
#include "drain.h"

#define CAP   1024000        /* 32 s at 32 kHz */
#define SR    32000
#define WIN   128000         /* 4 s loop */
#define HOLD  6              /* seconds the slider is held at the bottom */

static unsigned char buf[CAP];

/* Returns the sample index at which output goes permanently silent, or -1 if it never does.
 * `absolute` selects the replacement cursor (drain.h) over the one that shipped. */
static int run(int per, int absolute, int *audible_out) {
    memset(buf, 1, sizeof buf);
    int pos = 500000, off = 0, audible = 0, last_sound = -1;
    drain_cursor_t dc; drain_restart(&dc);

    for (int n = 0; n < SR * HOLD; n++) {
        int read_pos = pos - WIN; if (read_pos < 0) read_pos += CAP;

        if (buf[read_pos]) { audible++; last_sound = n; }   /* this sample is heard */

        if (absolute) {
            for (int k = 0; k < per; k++) buf[drain_next(&dc, read_pos, WIN, CAP)] = 0;
        } else {
            for (int k = 0; k < per; k++) {
                if (++off >= WIN) off = 0;
                int idx = pos - off; while (idx < 0) idx += CAP;
                buf[idx] = 0;
            }
        }
        buf[pos] = 0;                       /* write head lays down silence for free */
        if (++pos >= CAP) pos = 0;
    }
    *audible_out = audible;
    return (last_sound == SR * HOLD - 1) ? -1 : last_sound + 1;
}

int main(void) {
    printf("holding the drain for %d s on a %d-sample (%.0f s) loop\n\n", HOLD, WIN, (double)WIN / SR);
    printf("  cursor      per   goes silent at   audible samples\n");
    int fail = 0;
    for (int per = 1; per <= 2; per++) {
        for (int absolute = 0; absolute <= 1; absolute++) {
            int audible, q = run(per, absolute, &audible);
            printf("  %-10s  %d   ", absolute ? "absolute" : "shipped", per);
            if (q < 0) printf("%-14s", "never");
            else       printf("%9.2f s    ", (double)q / SR);
            printf("   %d\n", audible);
            /* The claim under test: at the rate actually shipped, the absolute cursor gets in
               front of playback at once - the old loop is never heard. */
            if (absolute && per == LEAK_PER_SAMPLE && (q < 0 || q > SR / 10)) fail = 1;
        }
    }
    printf("\n(shipped rate is LEAK_PER_SAMPLE = %d)\n", LEAK_PER_SAMPLE);
    printf("%s\n", fail ? "FAIL: absolute cursor did not get in front of playback"
                        : "PASS: at LEAK_PER_SAMPLE the absolute cursor silences the loop at once");
    return fail;
}
