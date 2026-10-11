/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Runs an APK's DEX through the interpreter on the host and plays it, so a
 * change to the layer can be judged by what the game does rather than by whether
 * it compiled. Written against FlappyBird_64bit.apk.
 *
 *   dex-test [app.apk] [output-dir]
 *
 * It taps the title's first button, waits out the fade to Get Ready, then flaps,
 * and fails if the screen goes white before the game starts or stays white, if
 * the game never draws anything different, or if a touch changes nothing. PNGs
 * of key frames go to the output directory: look at them.
 *
 * Environment:
 *   TL_FRAMES=N         frames to run (default 90; 600+ is real gameplay)
 *   TL_AUTOPILOT=1      flap like a player instead of on a fixed beat;
 *   TL_LINE=Y             ... when the bird sinks below this height
 *   TL_SEEK=1             ... or, better, below the next gap's centre, as seen in the draws
 *   TL_FLAP_EVERY=N     fixed-beat flaps, every N frames (default 22)
 *   TL_BUTTON=0|1|2     which title button to tap (default 0, the first)
 *   TL_SAVE_EVERY=N     save a frame PNG every N frames past 120 (default 60)
 *   TL_STATE_RANGE=a-b  print the game state on every frame in a..b
 *   TL_PROGRESS=1       name each frame on stderr as it starts
 *   TL_DUMP_ARRAYS=1    print the view's array-typed fields at startup
 *   TL_SEED=n           fix Math.random's seed, so a run (its pipes) can be replayed
 *   TL_CG=1             draw through CoreGraphics only, to compare with the blitter
 *   TL_DUMP_RAW=1       also write each saved frame as raw RGBA, for diffing
 *   TL_HAMMER=1         send touches from a second thread, for -fsanitize=thread
 *   TL_TRACE_FRAME=a-b  print every instruction run in those frames. Needs a
 *                       build with -DTL_DEX_TRACE; the shipping build has none.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include "husk-tl-dex.h"

/* Write the framebuffer as a PNG, so what the game drew can be looked at
 * rather than inferred from a pixel count. */
static void save_png(const uint32_t *fb, int width, int height, const char *path)
{
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate((void *)fb, width, height, 8, width * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(c);
    CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, str, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    if (dest && img) {
        CGImageDestinationAddImage(dest, img, NULL);
        CGImageDestinationFinalize(dest);
    }
    if (dest) CFRelease(dest);
    if (url) CFRelease(url);
    if (str) CFRelease(str);
    if (img) CGImageRelease(img);
    if (c) CGContextRelease(c);
    CGColorSpaceRelease(cs);
}

/* FNV-1a over the whole framebuffer. Two frames with the same hash are the same
 * picture; counting distinct hashes is how "the game is running" is told apart
 * from "the game drew one frame and stopped". */
static uint64_t frame_hash(const uint32_t *fb, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= fb[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The hit boxes the title screen tests a touch against (see the trace of
 * onTouchEvent): three Rect fields. They are laid out lazily, on the first
 * frames, so they are read when the touch is about to happen -- read earlier,
 * every one is zero. Returns where to tap: the centre of the chosen box, or the
 * middle of the screen if there is none. */
/* TL_HAMMER=1: a second thread sends touches as fast as it can for the whole
 * run, the way a UI thread does while the frame pump is mid-frame. Meant to be
 * run under -fsanitize=thread: with input delivered where it arrived, this is a
 * data race on every touch. */
static tl_dex_context *g_hammer_ctx;
static atomic_int g_hammer_stop;
static void *hammer(void *arg)
{
    (void)arg;
    unsigned seed = 12345;
    while (!atomic_load(&g_hammer_stop)) {
        float x = (float)(rand_r(&seed) % 540), y = (float)(rand_r(&seed) % 960);
        tl_dex_send_touch(g_hammer_ctx, 0, x, y);
        tl_dex_send_touch(g_hammer_ctx, 2, x + 1, y);
        tl_dex_send_touch(g_hammer_ctx, 1, x, y);
        usleep(1000);
    }
    return NULL;
}

/* The pipes drawn in the frame just ticked: 97x937 sprites, top and bottom. */
#define PIPE_W 97
#define PIPE_H 937
static struct { float x, y; } g_pipes[32];
static int g_npipes;
static void observe_draw(void *user, int w, int h, float x, float y)
{
    (void)user;
    if (w == PIPE_W && h == PIPE_H && g_npipes < 32) { g_pipes[g_npipes].x = x; g_pipes[g_npipes].y = y; g_npipes++; }
}

/*
 * Where the next gap's centre is, from the pipes just drawn. A pipe is two
 * sprites at the same x -- the top one hanging from above, the bottom one
 * standing up -- so the gap is between the top sprite's lower edge and the
 * bottom sprite's top. Returns false if no pipe is still ahead of the bird.
 */
/* Vertical drift: the same pipe, a frame apart, should be at the same height.
 * A pipe scrolls left a few pixels a frame, so last frame's sprite for it is the
 * one a little to the right of this frame's. Any difference in height between
 * the two is the pipe moving up or down -- which these pipes must not do. */
static float g_prev_x[32], g_prev_y[32];
static int g_prev_n;
static int g_wobbles;
static float g_max_wobble;
static void track_pipe_drift(void)
{
    for (int i = 0; i < g_npipes; i++) {
        for (int j = 0; j < g_prev_n; j++) {
            float dx = g_prev_x[j] - g_pipes[i].x;
            bool same_kind = (g_prev_y[j] < 0) == (g_pipes[i].y < 0);
            if (same_kind && dx > 0.2f && dx < 15.0f) {
                float dy = fabsf(g_prev_y[j] - g_pipes[i].y);
                if (dy > 0.5f) { g_wobbles++; if (dy > g_max_wobble) g_max_wobble = dy; }
                break;
            }
        }
    }
    g_prev_n = g_npipes;
    for (int i = 0; i < g_npipes; i++) { g_prev_x[i] = g_pipes[i].x; g_prev_y[i] = g_pipes[i].y; }
}

static bool next_gap_centre(float bird_left, float *centre)
{
    float best_x = 1e9f, top = 0, bottom = 0;
    for (int i = 0; i < g_npipes; i++) {
        if (g_pipes[i].x + PIPE_W <= bird_left) continue;           /* already behind it */
        if (g_pipes[i].x > best_x + 0.5f) continue;
        if (g_pipes[i].x < best_x - 0.5f) { best_x = g_pipes[i].x; top = -1e9f; bottom = 1e9f; }
        if (g_pipes[i].y < 0) top = g_pipes[i].y + PIPE_H; else bottom = g_pipes[i].y;
    }
    if (best_x > 1e8f || top < -1e8f || bottom > 1e8f) return false;
    *centre = (top + bottom) / 2;
    return true;
}

static float g_touch_x = 270.0f, g_touch_y = 480.0f;

static void pick_touch(tl_dex_class *c_class, tl_dex_object *view_obj, int which,
                       float *x, float *y)
{
    static const char *names[3] = { "aj", "ak", "al" };
    float boxes[3][4] = {{0}};
    int n = 0;
    for (int i = 0; i < 3; i++) {
        tl_dex_field *bf = tl_dex_find_field(c_class, names[i], "Landroid/graphics/Rect;");
        if (!bf || bf->slot >= view_obj->nfields) continue;
        tl_dex_object *r = view_obj->fields[bf->slot].l;
        if (!r || r->nfields != 0 || !r->native_ptr) continue;   /* not a native Rect */
        memcpy(boxes[n], r->native_ptr, sizeof(float) * 4);
        printf("  button %s: left=%.0f top=%.0f right=%.0f bottom=%.0f\n", names[i],
               boxes[n][0], boxes[n][1], boxes[n][2], boxes[n][3]);
        n++;
    }
    if (which >= 0 && which < n) {
        *x = (boxes[which][0] + boxes[which][2]) / 2;
        *y = (boxes[which][1] + boxes[which][3]) / 2;
    }
}

int main(int argc, char **argv)
{
    /* Unbuffered: this is a test of code that can crash, and a crash discards
     * whatever stdio was still holding -- which is exactly the output wanted. */
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *apk = (argc > 1) ? argv[1] : "/Users/davi/Documents/FlappyBird_64bit.apk";
    const char *outdir = (argc > 2) ? argv[2] : "/tmp";
    char path[1024];

    printf("=== Testing Husk DEX Interpreter & Framework Shims ===\n");
    printf("Loading APK: %s\n", apk);

    int width = 540;
    int height = 960;
    uint32_t *fb = calloc(width * height, sizeof(uint32_t));

    tl_dex_context *ctx = tl_dex_context_create(apk, fb, width, height);
    if (!ctx) {
        fprintf(stderr, "Failed to create DEX context\n");
        return 1;
    }

    if (!tl_dex_load_apk(ctx, apk)) {
        fprintf(stderr, "Failed to load DEX files from APK\n");
        tl_dex_context_destroy(ctx);
        return 1;
    }

    /* TL_SEED=n replays a run -- the same pipes -- and TL_CG=1 draws through
     * CoreGraphics only, so the two renderers can be compared on the same game. */
    ctx->draw_observer = observe_draw;
    if (getenv("TL_SEED")) tl_dex_seed(ctx, (uint64_t)strtoull(getenv("TL_SEED"), NULL, 10));
    if (getenv("TL_CG")) ctx->use_cg_only = 1;
    printf("Total classes loaded: %d\n", ctx->num_classes);

    /* Look up Flappy Bird View class 'c' */
    tl_dex_class *c_class = tl_dex_find_class(ctx, "Lcom/flappybird/recreation/c;");
    if (!c_class) {
        fprintf(stderr, "Could not find class Lcom/flappybird/recreation/c;\n");
        tl_dex_context_destroy(ctx);
        return 1;
    }

    printf("Found game view class: %s (fields=%d, methods=%d)\n",
           c_class->descriptor, c_class->num_fields, c_class->num_methods);

    /* Allocate game view object */
    tl_dex_object *view_obj = tl_dex_alloc_object(c_class);
    ctx->current_view = view_obj;

    /* 1. Call c.<init>(Context) */
    tl_dex_method *init_m = tl_dex_find_method(c_class, "<init>", "VL");
    if (init_m) {
        printf("Invoking c.<init>...\n");
        tl_dex_val args[2];
        args[0].l = view_obj;
        args[1].l = ctx->current_activity;
        ctx->trace = getenv("TL_TRACE_INIT") != NULL;     /* the constructor reads the saved settings */
        tl_dex_invoke(ctx, init_m, args, 2, NULL);
        ctx->trace = 0;
        printf("c.<init> completed successfully!\n");
    }

    /* Initialize Window insets (top and bottom bars) so layout 'e' proceeds */
    tl_dex_field *fi = tl_dex_find_field(c_class, "i", "I");
    tl_dex_field *fj = tl_dex_find_field(c_class, "j", "I");
    if (fi && view_obj->fields) view_obj->fields[fi->slot].i = 0;
    if (fj && view_obj->fields) view_obj->fields[fj->slot].i = 0;

    /* 2. Call c.onSizeChanged(540, 960, 0, 0) */
    tl_dex_method *size_m = tl_dex_find_method(c_class, "onSizeChanged", "VIIII");
    if (size_m) {
        printf("Invoking c.onSizeChanged(540, 960, 0, 0)...\n");
        tl_dex_val args[5];
        args[0].l = view_obj;
        args[1].i = width;
        args[2].i = height;
        args[3].i = 0;
        args[4].i = 0;
        ctx->trace = getenv("TL_TRACE_SETUP") != NULL;   /* layout is where rects get their values */
        tl_dex_invoke(ctx, size_m, args, 5, NULL);
        ctx->trace = 0;
        printf("c.onSizeChanged completed!\n");
    }

    /* 2b. Start game loop: invoke c.b() */
    tl_dex_method *start_m = tl_dex_find_method(c_class, "b", NULL);
    if (start_m) {
        printf("Invoking c.b() to start game loop...\n");
        tl_dex_val args[1];
        args[0].l = view_obj;
        tl_dex_invoke(ctx, start_m, args, 1, NULL);
        printf("c.b() completed!\n");
    }

    int nz = 0;
    for (int i = 0; i < width * height; i++) if (fb[i]) nz++;
    printf("After c.b(): non-zero pixels = %d\n", nz);

    /* Dump every array-typed field of the view and what its elements are.
     * Objects carry no kind tag, so "an array where a Bitmap should be" is
     * invisible to the interpreter and only shows up as a crash somewhere
     * downstream; printing the shape of the data is how it gets seen. */
    if (getenv("TL_DUMP_ARRAYS")) {
        printf("array fields of %s:\n", c_class->descriptor);
        for (tl_dex_class *k = c_class; k; k = k->super_class) {
            for (int i = 0; i < k->num_fields; i++) {
                tl_dex_field *fl = &k->fields[i];
                if (fl->is_static || !fl->type || fl->type[0] != '[') continue;
                if (fl->slot >= view_obj->nfields) continue;
                tl_dex_object *a = view_obj->fields[fl->slot].l;
                printf("  slot %2u %-6s %-34s ", fl->slot, fl->name, fl->type);
                if (!a) { printf("null\n"); continue; }
                printf("array len=%u elem=%u:", a->array.length, a->array.elem_size);
                if (fl->type[1] == 'L' || fl->type[1] == '[') {
                    for (uint32_t e = 0; e < a->array.length && e < 6; e++) {
                        tl_dex_object *el = ((tl_dex_object **)a->array.elements)[e];
                        printf(" [%u]=%s", e, !el ? "null" : "obj@set");
                    }
                }
                printf("\n");
            }
        }
    }

    /* What state machine is the game in? `c.a` holds the current state, an enum
     * from class d; print the constants d defines and which one c is sitting on,
     * so "the screen did not change" can be told apart from "the game is in a
     * state that does not draw anything different". */
    tl_dex_class *state_class = tl_dex_find_class(ctx, "Lcom/flappybird/recreation/d;");
    tl_dex_field *state_field = tl_dex_find_field(c_class, "a", "Lcom/flappybird/recreation/d;");
    if (state_class) {
        printf("state enum d: %d static fields\n", state_class->num_static_fields);
        for (int i = 0; i < state_class->num_fields; i++) {
            tl_dex_field *sf = &state_class->fields[i];
            if (!sf->is_static || !state_class->static_values || sf->slot >= (uint32_t)state_class->num_static_fields)
                continue;
            tl_dex_object *k = state_class->static_values[sf->slot].l;
            if (sf->type && !strcmp(sf->type, "Lcom/flappybird/recreation/d;")) {
                printf("  d.%s = ordinal %d (%s)\n", sf->name,
                       (k && k->nfields > TL_ENUM_SLOT_ORDINAL) ? k->fields[TL_ENUM_SLOT_ORDINAL].i : -1,
                       k ? "object" : "NULL -- <clinit> has not run");
            }
        }
    }

    /* 3. Run the game loop, and measure what it does. */
    /* TL_FRAMES=N runs longer; past the Get Ready screen (about frame 90) the
     * harness flaps every TL_FLAP_EVERY frames, so a longer run is actual
     * gameplay -- pipes, scoring -- and not a bird falling on the title. */
    enum { TOUCH_DOWN = 30, TOUCH_UP = 31 };
    const int FRAMES = getenv("TL_FRAMES") ? atoi(getenv("TL_FRAMES")) : 90;
    const int FLAP_EVERY = getenv("TL_FLAP_EVERY") ? atoi(getenv("TL_FLAP_EVERY")) : 22;
    const bool autopilot = getenv("TL_AUTOPILOT") != NULL;
    const float autopilot_line = getenv("TL_LINE") ? (float)atof(getenv("TL_LINE")) : 430.0f;
    tl_dex_field *bird_y = tl_dex_find_field(c_class, "ae", "F");
    int flaps = 0;
    pthread_t hammer_thread;
    bool hammering = getenv("TL_HAMMER") != NULL;
    if (hammering) {
        g_hammer_ctx = ctx;
        pthread_create(&hammer_thread, NULL, hammer, NULL);
    }
    printf("Running %d game loop frames...%s%s\n", FRAMES, autopilot ? " (autopilot)" : "",
           hammering ? " (touch hammer on a second thread)" : "");
    uint64_t *hashes = calloc((size_t)FRAMES + 1, sizeof(uint64_t));
    /* White is only a failure if it appears before the game has started or
     * stays. A short white flash is what Flappy Bird does when the bird dies,
     * and the original bug (an enum that was never initialised) painted the
     * screen white on frame 2 and left it there. */
    bool went_white = false;
    int white_run = 0, longest_white = 0, first_white = -1;
    /* Per-state cost of a frame, from the layer's own counters. The harness
     * runs unthrottled, so this is what a frame costs, not how fast it was shown. */
    struct { uint64_t n, logic, render, draw, draws; } cost[8] = {{0}};
    uint64_t nanos = 1000000000ULL;
    for (int f = 0; f < FRAMES; f++) {
        nanos += 16666666ULL; /* ~60 fps */
        /* TL_TRACE_FRAME=N prints every instruction executed during frame N
         * (needs a -DTL_DEX_TRACE build). TL_PROGRESS=1 names each frame as it
         * starts, so a crash can be placed. */
        const char *tf = getenv("TL_TRACE_FRAME");
        if (getenv("TL_PROGRESS")) { fprintf(stderr, "[frame %d]\n", f); }
        /* TL_TRACE_FRAME=N, or a range N-M. */
        ctx->trace = 0;
        if (tf) {
            int lo = atoi(tf), hi = lo;
            const char *dash = strchr(tf, '-');
            if (dash) hi = atoi(dash + 1);
            ctx->trace = (f >= lo && f <= hi);
        }
        g_npipes = 0;
        struct tl_dex_perf before = ctx->perf;
        int state_before = 7;
        if (state_field && view_obj->nfields > state_field->slot) {
            tl_dex_object *st0 = view_obj->fields[state_field->slot].l;
            if (st0 && st0->nfields > TL_ENUM_SLOT_ORDINAL) state_before = st0->fields[TL_ENUM_SLOT_ORDINAL].i & 7;
        }
        tl_dex_tick_frame(ctx, nanos);
        track_pipe_drift();
        cost[state_before].n++;
        cost[state_before].logic  += ctx->perf.ns_logic  - before.ns_logic;
        cost[state_before].render += ctx->perf.ns_render - before.ns_render;
        cost[state_before].draw   += ctx->perf.ns_draw   - before.ns_draw;
        cost[state_before].draws  += ctx->perf.draws     - before.draws;
        /* Left on through the touch below: the input handler is as much a part
         * of the frame as the tick, and it is what usually needs tracing. It is
         * switched off at the top of the next iteration. */
        hashes[f] = frame_hash(fb, (size_t)width * height);
        if (fb[0] == 0xffffffffu) {
            went_white = true;
            if (first_white < 0) first_white = f;
            if (++white_run > longest_white) longest_white = white_run;
        } else {
            white_run = 0;
        }

        if (state_field && view_obj->nfields > state_field->slot &&
            (f < 3 || f == TOUCH_DOWN - 1 || f == TOUCH_DOWN + 1 || f == TOUCH_DOWN + 10 ||
             f == FRAMES - 1 || f % 60 == 0 ||
             (getenv("TL_STATE_RANGE") && f >= atoi(getenv("TL_STATE_RANGE")) &&
              f <= atoi(strchr(getenv("TL_STATE_RANGE"), '-') + 1)))) {
            tl_dex_object *st = view_obj->fields[state_field->slot].l;
            printf("  frame %2d: game state ordinal = %d\n", f,
                   (st && st->nfields > TL_ENUM_SLOT_ORDINAL) ? st->fields[TL_ENUM_SLOT_ORDINAL].i : -1);
        }

        const int save_every = getenv("TL_SAVE_EVERY") ? atoi(getenv("TL_SAVE_EVERY")) : 60;
        if (f == 0 || f == TOUCH_DOWN - 1 || f == 60 || f == FRAMES - 1 ||
            (f >= 120 && f % save_every == 0)) {
            snprintf(path, sizeof(path), "%s/flappy_frame_%03d.png", outdir, f);
            save_png(fb, width, height, path);
            if (getenv("TL_DUMP_RAW")) {     /* the exact bytes, for diffing two runs */
                char rawpath[1024];
                snprintf(rawpath, sizeof(rawpath), "%s/flappy_frame_%03d.raw", outdir, f);
                FILE *rf = fopen(rawpath, "wb");
                if (rf) { fwrite(fb, 4, (size_t)width * height, rf); fclose(rf); }
            }
            printf("Frame %2d: hash=%016llx fb[0]=0x%08x  -> %s\n", f,
                   (unsigned long long)hashes[f], fb[0], path);
        }

        if (f == TOUCH_DOWN) {
            float touch_x = 270.0f, touch_y = 480.0f;
            const char *which = getenv("TL_BUTTON");
            pick_touch(c_class, view_obj, which ? atoi(which) : 0, &touch_x, &touch_y);
            printf("Frame %d: touch down at (%.0f, %.0f)\n", f, touch_x, touch_y);
            g_touch_x = touch_x; g_touch_y = touch_y;
            ctx->trace = getenv("TL_TRACE_TOUCH") != NULL;
            tl_dex_send_touch(ctx, 0 /* ACTION_DOWN */, g_touch_x, g_touch_y);
            ctx->trace = 0;
        } else if (autopilot && f > 95) {
            /* Flap when the bird sinks below a line, the way a person does, and
             * never twice in quick succession. `ae` is the bird's height: the
             * trace of the collision check read it just before measuring the
             * bird's bitmap. */
            static int last_flap = -100, flapping = 0, started = 0;
            float y = 0;
            /* Starting is a tap too: on Get Ready the bird only hovers, and
             * below the flap line a bot that waits for a reason never begins. */
            float gap_y;
            float line = autopilot_line;
            /* Aim a little below the gap's centre: after a flap the bird keeps
             * rising for several frames, so the line it flaps at is not where
             * it ends up. */
            if (getenv("TL_SEEK") && next_gap_centre(150.0f, &gap_y)) line = gap_y + 18.0f;
            if (!started && f >= 96) { started = 1; flapping = 0; last_flap = f;
                tl_dex_send_touch(ctx, 0, 270.0f, 480.0f); flapping = 1; flaps++; goto after_touch; }
            if (bird_y && bird_y->slot < view_obj->nfields) y = view_obj->fields[bird_y->slot].f;
            if (flapping) { tl_dex_send_touch(ctx, 1, 270.0f, 480.0f); flapping = 0; }
            else if (y > line && f - last_flap > 14) {
                tl_dex_send_touch(ctx, 0, 270.0f, 480.0f);
                flapping = 1; last_flap = f; flaps++;
            }
        } else if (!autopilot && f > 95 && f % FLAP_EVERY == 0) {
            /* A flap: down, and up on the next frame. */
            tl_dex_send_touch(ctx, 0 /* ACTION_DOWN */, 270.0f, 480.0f);
        } else if (!autopilot && f > 95 && f % FLAP_EVERY == 1) {
            tl_dex_send_touch(ctx, 1 /* ACTION_UP */, 270.0f, 480.0f);
        } else if (f == TOUCH_UP) {
            tl_dex_send_touch(ctx, 1 /* ACTION_UP */, g_touch_x, g_touch_y);
        }
        after_touch: ;
    }

    /* Distinct pictures before and after the touch. */
    int distinct_before = 0, distinct_after = 0;
    for (int i = 0; i < FRAMES; i++) {
        if (i < TOUCH_DOWN + 6) { /* fallthrough: counted below */ }
        bool seen = false;
        for (int j = 0; j < i; j++) if (hashes[j] == hashes[i]) { seen = true; break; }
        if (!seen) { if (i < TOUCH_DOWN) distinct_before++; else distinct_after++; }
    }
    bool responded = FRAMES > TOUCH_DOWN + 5 && hashes[TOUCH_DOWN + 5] != hashes[TOUCH_DOWN - 1];

    printf("\nframes delivered:        %llu\n", (unsigned long long)ctx->frame_count);
    printf("distinct pictures:       %d before the touch, %d after\n", distinct_before, distinct_after);
    printf("white frames:            %s (first at %d, longest run %d)\n",
           went_white ? "yes" : "none", first_white, longest_white);
    printf("changed after the touch: %s\n", responded ? "yes" : "NO");
    if (autopilot) printf("autopilot flaps:         %d\n", flaps);

    snprintf(path, sizeof(path), "%s/flappy_bird_frame.png", outdir);
    save_png(fb, width, height, path);

    static const char *state_names[8] = { "title", "fading", "get ready", "starting", "playing", "game over", "?", "?" };
    printf("\nframe cost by game state (ms per frame; render includes the draw calls)\n");
    printf("  %-10s %6s %8s %8s %8s %10s\n", "state", "frames", "logic", "render", "of which draw", "draws/frame");
    for (int i = 0; i < 8; i++) {
        if (!cost[i].n) continue;
        double n = (double)cost[i].n;
        printf("  %-10s %6.0f %8.3f %8.3f %8.3f %10.1f\n", state_names[i], n,
               cost[i].logic / n / 1e6, cost[i].render / n / 1e6, cost[i].draw / n / 1e6,
               cost[i].draws / n);
    }

    int failures = 0;
    /* Hammer mode is a race test, not a gameplay test: random touches from the
     * first frame start the game early and kill the bird at arbitrary moments,
     * so the gameplay criteria below would fail for the right reasons. What it
     * is judged on is surviving, and a sanitizer saying nothing. */
    if (hammering) goto verdict;
    if (went_white && first_white < 90) {
        printf("FAIL: the screen went white at frame %d, before the game began\n", first_white);
        failures++;
    }
    if (longest_white > 12) {
        printf("FAIL: the screen stayed white for %d frames\n", longest_white);
        failures++;
    }
    if (distinct_before + distinct_after < 3) {
        printf("FAIL: the game drew almost nothing different across %d frames -- it is not running\n", FRAMES);
        failures++;
    }
    if (!responded) { printf("FAIL: the touch changed nothing\n"); failures++; }

    /*
     * Two things the game once got wrong because a preference read back as zero.
     * The score multiplier came back 0, so passing a pipe scored nothing; the
     * thresholds at which pipes begin to move came back 0, so they bobbed from
     * the first frame. Neither shows in a screenshot of one moment, and neither
     * made the harness fail -- so both are asserted now.
     */
    printf("pipe drift:              %d frame-to-frame changes in height (largest %.1f px)\n",
           g_wobbles, g_max_wobble);
    if (g_wobbles > 0) {
        printf("FAIL: pipes moved vertically -- they are static in the real game\n");
        failures++;
    }
    if (getenv("TL_SEEK")) {
        tl_dex_field *score_f = tl_dex_find_field(c_class, "at", "I");
        int score = (score_f && score_f->slot < view_obj->nfields) ? view_obj->fields[score_f->slot].i : -1;
        printf("score reached:           %d\n", score);
        /* A bot that threads the gaps passes a pipe about every 1.3 seconds. */
        int expect = FRAMES >= 700 ? (FRAMES - 250) / 150 / 2 : 0;
        if (FRAMES >= 700 && score < (expect > 3 ? expect : 3)) {
            printf("FAIL: the score is %d after %d frames of flying through the gaps; it should be climbing\n",
                   score, FRAMES);
            failures++;
        }
    }
verdict:

    if (hammering) { atomic_store(&g_hammer_stop, 1); pthread_join(hammer_thread, NULL); }
    if (getenv("TL_DUMP_INTS")) {     /* every int field of the view, to find a value by its number */
        printf("int fields of the view at the end:");
        for (tl_dex_class *k = c_class; k; k = k->super_class)
            for (int i = 0; i < k->num_fields; i++) {
                tl_dex_field *fl = &k->fields[i];
                if (fl->is_static || !fl->type || strcmp(fl->type, "I") || fl->slot >= view_obj->nfields) continue;
                printf(" %s=%d", fl->name, view_obj->fields[fl->slot].i);
            }
        printf("\n");
    }
    free(hashes);
    tl_dex_context_destroy(ctx);
    free(fb);

    if (failures) {
        printf("=== DEX test FAILED (%d) ===\n", failures);
        return 1;
    }
    printf("=== DEX test passed ===\n");
    return 0;
}
