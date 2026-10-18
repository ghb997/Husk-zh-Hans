/*
 * ps4-test: runs a PS4 game through libhuskps4.dylib the way the app does -- Husk's JIT arena, a
 * bare CAMetalLayer, MoltenVK loaded by path, pads through the module -- on a Mac.
 *
 *   clang -fobjc-arc -O1 -o /tmp/ps4-test tools/ps4-test.m src/translation-layer-next/husk-console-host.c \
 *       src/translation-layer-next/husk-jit-arena.c -Isrc/translation-layer-next -Isrc/translation-layer -framework Foundation -framework QuartzCore -framework Metal
 *   /tmp/ps4-test <libhuskps4.dylib> <eboot.bin or game folder> [seconds]
 *   /tmp/ps4-test <libhuskps4.dylib> --install <package.pkg> <dest folder>   (what Husk's PS4 import does with a .pkg)
 *
 * Environment: PS4_PAD_SCRIPT="T:buttons:D,..." holds buttons (cross circle square triangle up down left right l1 r1 l2 r2
 * l3 r3 options share touchpad, joined with '+') from T to T+D seconds; PS4_USER_DIR (default: a folder in $TMPDIR);
 * PS4_MVK (default /opt/homebrew/lib/libMoltenVK.dylib); TL_XMEM_MIB sizes the JIT region (default 192);
 * PS4_TEXT answers the game's keyboard dialog; HUSK_MEM_LIMIT_MB reports a phone's available memory to the module; AETHER_SHOT_EVERY=<s> saves game screenshots into
 * the user dir's screenshots folder. PS4_PAD_FIFO=<path> makes a FIFO there and takes live presses from it, one per line,
 * "buttons seconds" (e.g. "cross 0.5", "down+cross 0.3"), to drive a game while watching its screenshots.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "husk-console-host.h"
#include "husk-jit-arena.h"
#include "husk-tl-gamepad.h"

/* The game's keyboard dialog: PS4_TEXT=<text> answers it a second later, as a player would; PS4_TEXT unset leaves the module
 * to answer for the player (an empty PS4_TEXT cancels). */
static int test_text_requester(const char *title, const char *initial, uint32_t max_chars, void (*done)(void *, const char *), void *ctx)
{
    const char *answer = getenv("PS4_TEXT");
    if (!answer) return -1;
    fprintf(stderr, "ps4-test: the game asked for text (title \"%s\", initial \"%s\", up to %u); answering \"%s\"\n",
            title ? title : "", initial ? initial : "", max_chars, answer);
    char *copy = strdup(answer);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{
        done(ctx, copy[0] ? copy : NULL);
        free(copy);
    });
    return 0;
}

void tl_log_line(const char *fmt, ...)
{
    (void)fmt; /* the console host already wrote the line to the log */
}

/* No Husk controllers here: PS4_PAD_SCRIPT drives the pad through husk_console_set_pad instead. */
bool tl_pad_read(int slot, tl_pad_state *out) { (void)slot; (void)out; return false; }
/* No Android engine here either. */
const char *husk_native_loaded_apk(void) { return NULL; }

typedef struct { double start, end; uint32_t buttons; } pad_event;

static uint32_t button_bits(const char *name)
{
    static const struct { const char *n; uint32_t b; } k[] = {
        {"share", 0x1}, {"l3", 0x2}, {"r3", 0x4}, {"options", 0x8}, {"up", 0x10}, {"right", 0x20}, {"down", 0x40}, {"left", 0x80},
        {"l2", 0x100}, {"r2", 0x200}, {"l1", 0x400}, {"r1", 0x800}, {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000},
        {"square", 0x8000}, {"touchpad", 0x100000},
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (!strcmp(k[i].n, name)) return k[i].b;
    fprintf(stderr, "ps4-test: unknown button %s\n", name);
    return 0;
}

static int parse_script(const char *s, pad_event *ev, int max)
{
    int n = 0;
    char *copy = strdup(s), *save = NULL;
    for (char *e = strtok_r(copy, ",", &save); e && n < max; e = strtok_r(NULL, ",", &save)) {
        char names[256]; double t, d;
        if (sscanf(e, "%lf:%255[^:]:%lf", &t, names, &d) != 3) { fprintf(stderr, "ps4-test: bad pad event %s\n", e); continue; }
        uint32_t bits = 0; char *s2 = NULL;
        for (char *b = strtok_r(names, "+", &s2); b; b = strtok_r(NULL, "+", &s2)) bits |= button_bits(b);
        ev[n++] = (pad_event){ t, t + d, bits };
    }
    free(copy);
    return n;
}

static void install_progress(void *ctx, uint64_t done, uint64_t total)
{
    (void)ctx;
    static int last = -1;
    int pct = total ? (int)(done * 100 / total) : 0;
    if (pct != last) { last = pct; fprintf(stderr, "\rinstalling %d%%", pct); }
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 5 && !strcmp(argv[2], "--install")) {
            char err[512] = "";
            bool ok = husk_console_install_package(argv[1], argv[3], argv[4], install_progress, NULL, err, sizeof err);
            fprintf(stderr, "\n%s\n", ok ? "installed" : err);
            return ok ? 0 : 1;
        }
        if (argc < 3) { fprintf(stderr, "usage: %s <libhuskps4.dylib> <eboot.bin or game folder> [seconds]\n", argv[0]); return 2; }
        int secs = argc > 3 ? atoi(argv[3]) : 60;
        const int w = 1280, h = 720;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = MTLCreateSystemDefaultDevice();
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.contentsScale = 1;
        layer.bounds = CGRectMake(0, 0, w, h);
        layer.drawableSize = CGSizeMake(w, h);

        NSString *user = getenv("PS4_USER_DIR") ? @(getenv("PS4_USER_DIR")) : [NSTemporaryDirectory() stringByAppendingPathComponent:@"husk-ps4-test"];
        [[NSFileManager defaultManager] createDirectoryAtPath:user withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *log = [user stringByAppendingPathComponent:@"native-run.log"];
        const char *mvk = getenv("PS4_MVK") ? getenv("PS4_MVK") : "/opt/homebrew/lib/libMoltenVK.dylib";
        size_t region = getenv("TL_XMEM_MIB") ? 0 : (size_t)192 << 20;

        husk_console_launch_args a = {
            .module_path = argv[1], .game_id = "ps4:test", .game = argv[2], .game_args = NULL, .user_dir = user.UTF8String, .log_path = log.UTF8String,
            .vulkan_path = mvk, .metal_layer = (__bridge void *)layer, .width_px = w, .height_px = h, .scale = 1.0f, .region_bytes = region,
        };
        char err[512] = "";
        husk_console_set_text_requester(test_text_requester);
        if (!husk_console_launch(&a, err, sizeof(err))) { fprintf(stderr, "ps4-test: launch failed: %s\n", err); return 1; }
        fprintf(stderr, "ps4-test: launched; log %s\n", log.UTF8String);

        pad_event ev[1024];
        int nev = getenv("PS4_PAD_SCRIPT") ? parse_script(getenv("PS4_PAD_SCRIPT"), ev, 256) : 0;
        int fifo = -1;
        char fifo_buf[512];
        size_t fifo_len = 0;
        if (getenv("PS4_PAD_FIFO")) {
            unlink(getenv("PS4_PAD_FIFO"));
            mkfifo(getenv("PS4_PAD_FIFO"), 0600);
            fifo = open(getenv("PS4_PAD_FIFO"), O_RDWR | O_NONBLOCK);   /* RDWR: never sees EOF between writers */
        }
        uint32_t applied = 0;
        struct timespec t0, t;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int last_state = -1;
        for (int tick = 0;; tick++) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            clock_gettime(CLOCK_MONOTONIC, &t);
            double now = (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
            if (fifo >= 0) {
                ssize_t r = read(fifo, fifo_buf + fifo_len, sizeof(fifo_buf) - 1 - fifo_len);
                if (r > 0) fifo_len += (size_t)r;
                char *nl;
                while (fifo_len && (nl = memchr(fifo_buf, '\n', fifo_len))) {
                    *nl = 0;
                    char names[256]; double d = 0.3;
                    if (sscanf(fifo_buf, "%255s %lf", names, &d) >= 1 && nev < 1024) {
                        uint32_t b = 0; char *s2 = NULL;
                        for (char *t = strtok_r(names, "+", &s2); t; t = strtok_r(NULL, "+", &s2)) b |= button_bits(t);
                        ev[nev++] = (pad_event){ now, now + d, b };
                        fprintf(stderr, "ps4-test: t=%.1fs fifo press %s for %.2fs\n", now, fifo_buf, d);
                    }
                    size_t used = (size_t)(nl - fifo_buf) + 1;
                    memmove(fifo_buf, nl + 1, fifo_len - used);
                    fifo_len -= used;
                }
                if (fifo_len >= sizeof(fifo_buf) - 1) fifo_len = 0;
            }
            uint32_t bits = 0;
            for (int i = 0; i < nev; i++) if (now >= ev[i].start && now < ev[i].end) bits |= ev[i].buttons;
            if (bits != applied) {
                HuskPadV1 pad = { .buttons = bits, .left_x = 128, .left_y = 128, .right_x = 128, .right_y = 128 };
                husk_console_set_pad(&pad);
                applied = bits;
            }
            int state = husk_console_state();
            if (state != last_state || tick % 500 == 0) {
                fprintf(stderr, "ps4-test: t=%.1fs state %d, %llu frames, JIT %zu KiB in use (high %zu KiB)\n", now, state,
                        (unsigned long long)husk_console_frames(), husk_jit_arena_used_bytes() >> 10, husk_jit_arena_high_water_bytes() >> 10);
                last_state = state;
            }
            if (state == HUSK_CONSOLE_FAILED || state == HUSK_CONSOLE_STOPPED) break;
            if (now >= secs) {
                fprintf(stderr, "ps4-test: %d s up, stopping\n", secs);
                husk_console_stop();
                for (int i = 0; i < 500 && husk_console_state() != HUSK_CONSOLE_STOPPED; i++)
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                break;
            }
        }
        fprintf(stderr, "ps4-test: end: state %d, %llu frames, JIT high water %zu KiB, %zu refusals\n", husk_console_state(),
                (unsigned long long)husk_console_frames(), husk_jit_arena_high_water_bytes() >> 10, husk_jit_arena_refusals());
        _exit(husk_console_state() == HUSK_CONSOLE_FAILED ? 1 : 0);
    }
}
