/* The app's launcher on a Mac, running a cocos2d-x game into a bare CAMetalLayer (no window): the window-surface path the
 * phone takes and the off-screen harness (cocos-test.c) never does. Landscape, like the game. Taps the screen on a timer
 * and prints the runtime's own state.
 *
 *   cocos-layer <apk> <cacert.pem> <libEGL.dylib> [seconds]
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
        NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"husk-cocos-layer-data"];
        [[NSFileManager defaultManager] createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
        if (!husk_cocos_launch(argv[1], data.UTF8String, (__bridge void *)layer, 1748, 804, argv[3], argv[2])) { fprintf(stderr, "launch refused\n"); return 1; }
        for (int i = 0; i < secs; i++) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
            husk_unity_perf perf; husk_unity_perf_snapshot(&perf);
            fprintf(stderr, "main: t=%ds state %d, %lu frames, %.0f fps, %.1f ms (max %.0f)\n", i + 1, husk_unity_state(), husk_unity_frames(), perf.fps, perf.mean_ms, perf.max_ms);
            /* a script that walks the menus: accept the notice, then play, then into the first level, then jump now and then */
            if (i == 6)  { husk_unity_touch(0, 0, 1200, 804 * 0.73f); husk_unity_touch(2, 0, 1200, 804 * 0.73f); }
            if (i == 9)  { husk_unity_touch(0, 0, 874, 804 * 0.48f);  husk_unity_touch(2, 0, 874, 804 * 0.48f); }
            if (i == 12) { husk_unity_touch(0, 0, 874, 804 * 0.31f);  husk_unity_touch(2, 0, 874, 804 * 0.31f); }
            if (i >= 20 && i % 2 == 0) { husk_unity_touch(0, 0, 874, 500); husk_unity_touch(2, 0, 874, 500); }
        }
        fprintf(stderr, "main: state %d, %lu frames\n", husk_unity_state(), husk_unity_frames());
    }
    return 0;
}
