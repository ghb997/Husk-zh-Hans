/* The app's launcher on a Mac, running Minecraft (GameActivity) into a bare CAMetalLayer (no window): the window-surface path
 * the phone takes and the off-screen harness (ga-test.c) never does. Landscape, like the game. Walks the first menus on a
 * timer and prints the runtime's own state.
 *
 *   ga-layer <apk> <cacert.pem> <libEGL.dylib> [seconds]
 */
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "husk-tl-unity-app.h"

extern int tl_log_sink_fd;
int tl_log_sink_fd = -1;
void tl_log_line(const char *fmt, ...)
{
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    static struct timespec t0;
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    pthread_mutex_lock(&m);
    if (!t0.tv_sec) t0 = t;
    char line[2048];
    int n = snprintf(line, sizeof(line), "[%7.3f] ", (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9);
    va_list ap; va_start(ap, fmt); int w = vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap); va_end(ap);
    if (w > (int)(sizeof(line) - n - 3)) w = (int)(sizeof(line) - n - 3);
    n += w;
    line[n++] = '\n';
    fwrite(line, 1, n, stderr);
    if (tl_log_sink_fd >= 0) { ssize_t r = write(tl_log_sink_fd, line, n); (void)r; }
    pthread_mutex_unlock(&m);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 4) { fprintf(stderr, "usage: %s <apk> <cacert.pem> <libEGL.dylib> [seconds]\n", argv[0]); return 2; }
        int secs = argc > 4 ? atoi(argv[4]) : 30;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.contentsScale = 2;
        layer.bounds = CGRectMake(0, 0, 874, 402);
        layer.drawableSize = CGSizeMake(1748, 804);
        NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"husk-ga-layer-data"];
        [[NSFileManager defaultManager] createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
        if (!husk_gameactivity_launch(argv[1], data.UTF8String, (__bridge void *)layer, 1748, 804, argv[3], argv[2])) { fprintf(stderr, "launch refused\n"); return 1; }
        for (int i = 0; i < secs; i++) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
            husk_unity_perf perf; husk_unity_perf_snapshot(&perf);
            fprintf(stderr, "main: t=%ds state %d, %lu frames, %.0f fps, %.1f ms (max %.0f)\n", i + 1, husk_unity_state(), husk_unity_frames(), perf.fps, perf.mean_ms, perf.max_ms);
            /* The new-player flow: Get started, Survival, Peaceful, Next, Play -- by position, as the layout scales with the surface. */
            float W = 1748, H = 804;
            struct { int t; float x, y; } taps[] = { { 80, 0.50f, 0.594f }, { 86, 0.333f, 0.471f }, { 92, 0.133f, 0.471f }, { 98, 0.592f, 0.768f }, { 104, 0.598f, 0.703f } };
            for (size_t k = 0; k < sizeof(taps) / sizeof(taps[0]); k++)
                if (i == taps[k].t) { husk_unity_touch(0, 0, W * taps[k].x, H * taps[k].y); usleep(80000); husk_unity_touch(2, 0, W * taps[k].x, H * taps[k].y); }
            if (i == 140) { husk_unity_touch(0, 0, 400, 600); for (int m = 1; m <= 20; m++) { usleep(16000); husk_unity_touch(1, 0, 400 + m * 15, 600 - m * 5); } husk_unity_touch(2, 0, 700, 500); }
            if (i == 150) husk_unity_set_paused(true);
            if (i == 154) husk_unity_set_paused(false);
        }
        fprintf(stderr, "main: state %d, %lu frames\n", husk_unity_state(), husk_unity_frames());
    }
    return 0;
}
