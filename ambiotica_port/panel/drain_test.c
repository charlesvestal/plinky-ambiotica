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

/* The failure the sweep could never fix: the slider is held for `hold_s` - less than a full
 * pass on a long loop - and then RELEASED. The cursor stops; the read head does not. Returns
 * the time at which audio comes back after the release, or -1 if it stays silent.
 *
 * `gated` adds the clear mark: the mark is stamped every sample while the slider is down, and
 * a read older than the mark returns silence instead of buffer content. */
static double comeback(int hold_s, int gated) {
    memset(buf, 1, sizeof buf);
    int pos = 500000;
    drain_cursor_t dc; drain_restart(&dc); dc.since_clear = CAP;   /* buffer starts full */

    for (int n = 0; n < SR * 12; n++) {
        int read_pos = pos - WIN; if (read_pos < 0) read_pos += CAP;
        const int down = (n < SR * hold_s);

        if (down && gated) drain_mark_clear(&dc);

        /* What the player hears: silence if the mark says this sample predates the clear. */
        int heard = gated ? (!drain_stale(&dc, WIN) && buf[read_pos]) : buf[read_pos];
        if (heard && !down) return (double)(n - SR * hold_s) / SR;   /* came back after release */

        if (down) for (int k = 0; k < LEAK_PER_SAMPLE; k++)
            buf[drain_next(&dc, read_pos, WIN, CAP)] = 0;
        buf[pos] = 0;
        drain_tick(&dc, CAP);
        if (++pos >= CAP) pos = 0;
    }
    return -1.0;
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

    /* A sweep only runs while the gesture does, so releasing early leaves the read head to
       walk off the end of the cleared region. The clear mark is what actually fixes it. */
    printf("\nheld briefly, then RELEASED - when does the loop come back?\n\n");
    printf("  hold    sweep only        sweep + clear mark\n");
    for (int h = 1; h <= 3; h++) {
        double s = comeback(h, 0), g = comeback(h, 1);
        printf("  %d s   ", h);
        if (s < 0) printf("%-18s", "never");
        else       printf("came back +%.2f s   ", s);
        if (g < 0) printf("never\n");
        else     { printf("came back +%.2f s\n", g); fail = 1; }
    }
    printf("\n%s\n", fail ? "FAIL: loop returned after the slider was released"
                          : "PASS: cleared loop stays silent however briefly the slider is held");
    return fail;
}
