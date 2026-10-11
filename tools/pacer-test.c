/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Checks the frame pacer: that it holds sixty frames a second when the work
 * fits, that work is absorbed rather than added to the frame, that a frame too
 * big to fit simply runs slower, and that after a stall it does not sprint.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "husk-tl-dex.h"

static void busy_ms(double ms)
{
    uint64_t end = tl_dex_now_ns() + (uint64_t)(ms * 1e6);
    while (tl_dex_now_ns() < end) { }
}

/* Run `frames` frames of `work_ms` each; return the achieved rate. If a stall is
 * given, frame `stall_at` takes that long, and the shortest interval after it is
 * reported -- a pacer that sprints shows up as intervals near zero. */
static double run(const char *label, int frames, double work_ms, int stall_at, double stall_ms,
                  double *min_after_stall)
{
    tl_pacer p;
    tl_pacer_start(&p, 16666667ull);
    uint64_t t0 = tl_dex_now_ns(), prev = t0;
    double min_gap = 1e9;
    for (int f = 0; f < frames; f++) {
        busy_ms(f == stall_at ? stall_ms : work_ms);
        tl_pacer_wait(&p);
        uint64_t now = tl_dex_now_ns();
        if (stall_at >= 0 && f > stall_at && f < stall_at + 10) {
            double gap = (double)(now - prev) / 1e6;
            if (gap < min_gap) min_gap = gap;
        }
        prev = now;
    }
    double secs = (double)(tl_dex_now_ns() - t0) / 1e9;
    double fps = frames / secs;
    printf("  %-34s %6.2f fps\n", label, fps);
    if (min_after_stall) *min_after_stall = min_gap;
    return fps;
}

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void)
{
    printf("pacer, 60 Hz target\n");
    double a = run("1 ms of work a frame",           120, 1.0, -1, 0, NULL);
    double b = run("5 ms of work a frame",           120, 5.0, -1, 0, NULL);
    double c = run("12 ms of work a frame",          120, 12.0, -1, 0, NULL);
    double d = run("25 ms of work a frame (too much)", 40, 25.0, -1, 0, NULL);

    /* The old behaviour -- sleep a whole frame after the work -- for contrast. */
    {
        uint64_t t0 = tl_dex_now_ns();
        for (int f = 0; f < 60; f++) { busy_ms(5.0); struct timespec ts = {0, 16666667}; nanosleep(&ts, NULL); }
        double fps = 60 / ((double)(tl_dex_now_ns() - t0) / 1e9);
        printf("  %-34s %6.2f fps   <- what 5 ms cost before\n", "5 ms, sleep after work", fps);
    }

    CHECK(fabs(a - 60.0) < 1.5, "1 ms work should hold 60 fps, got %.2f", a);
    CHECK(fabs(b - 60.0) < 1.5, "5 ms work should hold 60 fps, got %.2f -- the work is being added to the frame", b);
    CHECK(fabs(c - 60.0) < 1.5, "12 ms work should hold 60 fps, got %.2f", c);
    CHECK(d > 36.0 && d < 41.0, "25 ms work should run at about 40 fps, got %.2f", d);

    double min_gap = 0;
    run("200 ms stall mid-run", 90, 2.0, 30, 200.0, &min_gap);
    printf("  shortest interval in the 10 frames after the stall: %.1f ms\n", min_gap);
    CHECK(min_gap > 10.0, "frames ran back to back after a stall (%.1f ms apart): the pacer sprinted to catch up", min_gap);

    printf(failures ? "=== pacer test FAILED (%d) ===\n" : "=== pacer test passed ===\n", failures);
    return failures ? 1 : 0;
}
