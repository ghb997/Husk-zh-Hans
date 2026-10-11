/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Checks the blitter against CoreGraphics, which is what it replaces.
 *
 * Where the two are meant to agree -- rectangles on whole pixels -- they must
 * agree to within rounding. Where they are meant to differ -- fractional edges,
 * which CoreGraphics anti-aliases and the blitter snaps -- it checks the
 * property that matters instead: that two rectangles sharing an edge leave no
 * seam.
 *
 *   blit-test          exit status 0 if everything held
 */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "husk-tl-blit.h"

enum { W = 128, H = 96 };

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* A premultiplied sprite with a mix of opaque, transparent and translucent pixels. */
static void make_sprite(uint32_t *px, int w, int h, int translucent)
{
    for (int i = 0; i < w * h; i++) {
        uint32_t a = translucent ? (rnd() % 3 == 0 ? 0 : (rnd() % 2 ? 255 : rnd() & 255)) : 255;
        uint32_t r = (rnd() & 255) * a / 255, g = (rnd() & 255) * a / 255, b = (rnd() & 255) * a / 255;
        px[i] = r | (g << 8) | (b << 16) | (a << 24);
    }
}

static int max_channel_diff(const uint32_t *a, const uint32_t *b, int n)
{
    int worst = 0;
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 32; c += 8) {
            int d = abs((int)((a[i] >> c) & 255) - (int)((b[i] >> c) & 255));
            if (d > worst) worst = d;
        }
    return worst;
}

/* The same draw through CoreGraphics: no interpolation, whole-pixel rectangle. */
static void cg_draw(uint32_t *fb, const uint32_t *sprite, int sw, int sh,
                    int dx, int dy, int dw, int dh, int alpha)
{
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(fb, W, H, 8, W * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGContextSetInterpolationQuality(c, kCGInterpolationNone);
    CGContextSetShouldAntialias(c, false);
    CGContextTranslateCTM(c, 0, H);
    CGContextScaleCTM(c, 1, -1);

    CGContextRef sc = CGBitmapContextCreate((void *)sprite, sw, sh, 8, sw * 4, cs,
                                            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(sc);
    CGContextSaveGState(c);
    CGContextSetAlpha(c, alpha / 255.0);
    CGContextTranslateCTM(c, dx, dy + dh);
    CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, CGRectMake(0, 0, dw, dh), img);
    CGContextRestoreGState(c);
    CGImageRelease(img); CGContextRelease(sc); CGContextRelease(c); CGColorSpaceRelease(cs);
}

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void)
{
    static uint32_t fb_a[W * H], fb_b[W * H], sprite[64 * 48];
    tl_blit_target t = { fb_b, W, H, W };

    /* 1. Whole-pixel draws agree with CoreGraphics. */
    printf("1. whole-pixel draws vs CoreGraphics\n");
    int worst_overall = 0;
    long changed_cg = 0, changed_blit = 0;
    for (int trial = 0; trial < 200; trial++) {
        int sw = 4 + rnd() % 60, sh = 4 + rnd() % 44;
        int translucent = trial & 1;
        make_sprite(sprite, sw, sh, translucent);
        int alpha = (trial % 5 == 0) ? 255 : 30 + rnd() % 226;
        /* The same opaque backdrop under both, so blending has something to blend with. */
        for (int i = 0; i < W * H; i++) fb_a[i] = fb_b[i] = 0xFF000000u | (rnd() & 0xFFFFFF);
        /* Integer scale up or 1:1 keeps nearest-neighbour unambiguous. */
        int k = 1 + rnd() % 3;
        int dw = sw * k, dh = sh * k;
        int dx = (int)(rnd() % (W + 20)) - 10, dy = (int)(rnd() % (H + 20)) - 10;
        static uint32_t before[W * H];
        memcpy(before, fb_a, sizeof(before));
        cg_draw(fb_a, sprite, sw, sh, dx, dy, dw, dh, alpha);
        tl_blit_draw(&t, sprite, sw, sh, tl_blit_image_is_opaque(sprite, sw, sh),
                     0, 0, (float)sw, (float)sh, (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh), alpha);
        int d = max_channel_diff(fb_a, fb_b, W * H);
        /* Both sides actually drew: otherwise "they agree" would mean nothing. */
        for (int i = 0; i < W * H; i++) { changed_cg += fb_a[i] != before[i]; changed_blit += fb_b[i] != before[i]; }
        if (d > worst_overall) worst_overall = d;
        CHECK(d <= 2, "trial %d: %dx%d sprite x%d at (%d,%d) alpha %d%s differs by %d", trial, sw, sh, k, dx, dy,
              alpha, translucent ? " translucent" : "", d);
    }
    printf("   worst channel difference across 200 draws: %d (rounding, allowed up to 2)\n", worst_overall);
    printf("   pixels changed: %ld by CoreGraphics, %ld by the blitter\n", changed_cg, changed_blit);
    CHECK(changed_cg > 20000 && changed_blit > 20000, "the draws changed almost nothing (%ld / %ld): the comparison would be vacuous", changed_cg, changed_blit);
    CHECK(changed_cg == changed_blit, "CoreGraphics changed %ld pixels and the blitter %ld", changed_cg, changed_blit);

    /* 2. Two tiles that share a fractional edge leave no seam. */
    printf("2. adjacent fractional tiles are seamless\n");
    {
        uint32_t tile[50 * 20];
        for (int i = 0; i < 50 * 20; i++) tile[i] = 0xFF0000FFu;       /* opaque red */
        for (int i = 0; i < W * H; i++) fb_b[i] = 0xFF00FF00u;         /* green behind */
        float edge = 40.75f;
        tl_blit_draw(&t, tile, 50, 20, true, 0, 0, 50, 20, edge - 50.0f, 10, edge, 30, 255);
        tl_blit_draw(&t, tile, 50, 20, true, 0, 0, 50, 20, edge, 10, edge + 50.0f, 30, 255);
        int gaps = 0;
        for (int y = 10; y < 30; y++)
            for (int x = 0; x < 91; x++)
                if (fb_b[y * W + x] != 0xFF0000FFu) gaps++;
        CHECK(gaps == 0, "%d background pixels showed through where tiles meet", gaps);
        printf("   background pixels visible between the tiles: %d\n", gaps);
    }

    /* 3. Clipping: partly and wholly outside the target, and degenerate rectangles. */
    printf("3. clipping and degenerate input\n");
    {
        uint32_t px[8 * 8];
        make_sprite(px, 8, 8, 0);
        for (int i = 0; i < W * H; i++) fb_b[i] = 0;
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, -1000, -1000, -900, -900, 255);
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, W + 5, H + 5, W + 50, H + 50, 255);
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, 10, 10, 10, 20, 255);        /* zero width */
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, 20, 20, 10, 10, 255);        /* inverted */
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 0, 0, 10, 10, 20, 20, 255);        /* empty source */
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, 10, 10, 20, 20, 0);          /* alpha 0 */
        tl_blit_draw(&t, px, 8, 8, true, -50, -50, 500, 500, 0, 0, 9, 9, 255);    /* source rect past the image */
        int touched = 0; for (int i = 0; i < W * H; i++) if (fb_b[i]) touched++;
        CHECK(touched > 0 && touched <= 9 * 9, "unexpected pixel count %d after degenerate draws", touched);
        printf("   no crash; %d pixels drawn by the one legitimate call\n", touched);
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, -4, -4, 4, 4, 255);          /* half off the corner */
        tl_blit_draw(&t, px, 8, 8, true, 0, 0, 8, 8, W - 4, H - 4, W + 4, H + 4, 255);
    }

    /* 4. Fill agrees with a CoreGraphics fill. */
    printf("4. rectangle fill vs CoreGraphics\n");
    {
        int worst = 0;
        for (int trial = 0; trial < 100; trial++) {
            for (int i = 0; i < W * H; i++) fb_a[i] = fb_b[i] = 0xFF000000u | (rnd() & 0xFFFFFF);
            uint32_t argb = ((rnd() % 256) << 24) | (rnd() & 0xFFFFFF);
            int x = rnd() % W, y = rnd() % H, w = 1 + rnd() % (W - x), h = 1 + rnd() % (H - y);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef c = CGBitmapContextCreate(fb_a, W, H, 8, W * 4, cs,
                                                   kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CGContextSetShouldAntialias(c, false);
            CGContextTranslateCTM(c, 0, H); CGContextScaleCTM(c, 1, -1);
            CGContextSetRGBFillColor(c, ((argb >> 16) & 255) / 255.0, ((argb >> 8) & 255) / 255.0,
                                     (argb & 255) / 255.0, (argb >> 24) / 255.0);
            CGContextFillRect(c, CGRectMake(x, y, w, h));
            CGContextRelease(c); CGColorSpaceRelease(cs);
            tl_blit_fill(&t, (float)x, (float)y, (float)(x + w), (float)(y + h), argb);
            int d = max_channel_diff(fb_a, fb_b, W * H);
            if (d > worst) worst = d;
            CHECK(d <= 2, "fill trial %d (0x%08x at %d,%d %dx%d) differs by %d", trial, argb, x, y, w, h, d);
        }
        printf("   worst channel difference across 100 fills: %d\n", worst);
    }

    printf(failures ? "=== blit test FAILED (%d) ===\n" : "=== blit test passed ===\n", failures);
    return failures ? 1 : 0;
}
