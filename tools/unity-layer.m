/* The app's launcher on a Mac, drawing into a bare CAMetalLayer (no window): the window-surface path the phone takes
 * and the off-screen harness (unity-test.c) never does. Prints the runtime's own state for a while. */
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "husk-tl-unity-app.h"
#include "husk-tl-ld.h"

extern int tl_log_sink_fd;
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
    if (w > (int)(sizeof(line) - n - 3)) w = (int)(sizeof(line) - n - 3);      /* vsnprintf returns the length it wanted */
    n += w;
    line[n++] = '\n';
    fwrite(line, 1, n, stderr);
    if (tl_log_sink_fd >= 0) { ssize_t r = write(tl_log_sink_fd, line, n); (void)r; }
    pthread_mutex_unlock(&m);
}

int tl_log_sink_fd = -1;

static void probe_x21(uint64_t *r) { tl_log_line("PROBE x21=%#llx x19=%#llx x20=%#llx sp-ish x29=%#llx", (unsigned long long)r[21], (unsigned long long)r[19], (unsigned long long)r[20], (unsigned long long)r[29]); }
static void *probe_installer(void *arg)
{
    (void)arg;
    tl_lib *L = NULL;
    while (!(L = tl_ld_find_lib("libunity.so"))) usleep(2000);
    static const uint64_t offs[] = { 0xc7221c, 0xc722e0, 0xc72300, 0xc72308, 0xc72360 };
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) if (!tl_ld_probe(L, offs[i], probe_x21)) fprintf(stderr, "probe %#llx failed\n", (unsigned long long)offs[i]);
    return NULL;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 4) { fprintf(stderr, "usage: %s <apk> <cacert.pem> <libEGL.dylib> [seconds]\n", argv[0]); return 2; }
        int secs = argc > 4 ? atoi(argv[4]) : 20;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.contentsScale = 2;
        layer.bounds = CGRectMake(0, 0, 402, 744);
        layer.drawableSize = CGSizeMake(804, 1488);
        NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"husk-unity-layer-data"];
        [[NSFileManager defaultManager] createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
        if (!husk_unity_launch(argv[1], data.UTF8String, (__bridge void *)layer, 804, 1488, argv[3], argv[2])) { fprintf(stderr, "launch refused\n"); return 1; }
        if (getenv("TL_X21_PROBES")) { pthread_t pt; pthread_create(&pt, NULL, probe_installer, NULL); }
        for (int i = 0; i < secs; i++) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]]; }
        fprintf(stderr, "main: state %d, %lu frames\n", husk_unity_state(), husk_unity_frames());
    }
    return 0;
}
