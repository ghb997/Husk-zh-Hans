/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "husk-tl-blit.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------- pixel arithmetic */

/*
 * Multiply two packed 8-bit channels by a/255, rounding correctly.
 *
 * `x` holds one channel in bits 0-7 and another in 16-23, so each sits in its own
 * 16-bit lane with eight bits of headroom: 255 * 255 + 128 fits in a lane, and
 * nothing carries into its neighbour. The (t + (t >> 8)) >> 8 step is the exact
 * divide-by-255 for that range, which a plain >> 8 would get wrong by up to one
 * on every blend and accumulate into a visible tint over many layers.
 */
static inline uint32_t mul255_x2(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 0x00800080u;
    return ((t + ((t >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
}

/* A premultiplied pixel with all four channels scaled by a/255. */
static inline uint32_t scale_pm(uint32_t p, uint32_t a)
{
    uint32_t rb = mul255_x2(p & 0x00FF00FFu, a);
    uint32_t ag = mul255_x2((p >> 8) & 0x00FF00FFu, a);
    return rb | (ag << 8);
}

/*
 * Source-over for premultiplied pixels: result = s + d * (1 - s.alpha).
 *
 * No clamping, because none is needed: premultiplied means each channel of `s`
 * is at most its alpha, and d scaled by (255 - alpha) leaves exactly the room
 * for it. Alpha is the high byte for the layout this file uses (red lowest).
 */
static inline uint32_t over_pm(uint32_t s, uint32_t d)
{
    uint32_t ia = 255u - (s >> 24);
    uint32_t rb = mul255_x2(d & 0x00FF00FFu, ia);
    uint32_t ag = mul255_x2((d >> 8) & 0x00FF00FFu, ia);
    return s + (rb | (ag << 8));
}

/* ------------------------------------------------------------- coverage */

/*
 * The pixels a span covers: those whose centres lie in [lo, hi). Pixel i has its
 * centre at i + 0.5, so the first covered pixel is ceil(lo - 0.5) and one past
 * the last is ceil(hi - 0.5). Two spans that share an edge share it exactly --
 * one ends where the next begins -- which is what keeps scrolling tiles
 * seamless whatever fraction they sit on.
 */
static inline int span_begin(float lo) { return (int)ceilf(lo - 0.5f); }
static inline int span_end(float hi)   { return (int)ceilf(hi - 0.5f); }

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ----------------------------------------------------------------- draw */

void tl_blit_draw(const tl_blit_target *dst,
                  const uint32_t *src, int src_w, int src_h, bool src_opaque,
                  float sx0, float sy0, float sx1, float sy1,
                  float dx0, float dy0, float dx1, float dy1,
                  int alpha)
{
    if (!dst || !dst->pixels || !src || src_w <= 0 || src_h <= 0 || alpha <= 0) return;
    if (!(dx1 > dx0) || !(dy1 > dy0)) return;
    if (alpha > 255) alpha = 255;

    /* The source rectangle, widened to whole pixels and held inside the image.
     * An atlas sprite is a rectangle of pixels; sampling a fraction outside it
     * would pull in its neighbour. */
    int isx0 = clampi((int)floorf(sx0), 0, src_w);
    int isy0 = clampi((int)floorf(sy0), 0, src_h);
    int isx1 = clampi((int)ceilf(sx1),  0, src_w);
    int isy1 = clampi((int)ceilf(sy1),  0, src_h);
    if (isx1 <= isx0 || isy1 <= isy0) return;

    /* Destination pixels, clipped to the target. */
    int x0 = span_begin(dx0), x1 = span_end(dx1);
    int y0 = span_begin(dy0), y1 = span_end(dy1);
    int cx0 = x0 < 0 ? 0 : x0;
    int cy0 = y0 < 0 ? 0 : y0;
    int cx1 = x1 > dst->width  ? dst->width  : x1;
    int cy1 = y1 > dst->height ? dst->height : y1;
    if (cx0 >= cx1 || cy0 >= cy1) return;

    /* Source pixels per destination pixel, in 16.16 fixed point. */
    const float xscale = (float)(isx1 - isx0) / (dx1 - dx0);
    const float yscale = (float)(isy1 - isy0) / (dy1 - dy0);
    const int64_t xstep = (int64_t)(xscale * 65536.0f + 0.5f);

    /* Where the first covered column lands in the source, by its centre. */
    const float u0 = (float)isx0 + ((float)cx0 + 0.5f - dx0) * xscale;
    const int64_t fx0 = (int64_t)(u0 * 65536.0f);

    /* An unscaled, unmodified, opaque draw is a row copy: the common case for
     * the big background layers, and the one that matters most for speed.
     *
     * Exactly 1:1 means the source column for destination column i is
     * floor(u0) + i whatever fraction the rectangle sits on -- adding a whole
     * number never changes the fractional part -- so the offset needs no
     * special-casing, only the step. */
    const bool copy_rows = src_opaque && alpha == 255 && xstep == 65536;
    const int copy_base = (int)(fx0 >> 16);
    const int copy_w = cx1 - cx0;

    for (int y = cy0; y < cy1; y++) {
        float v = (float)isy0 + ((float)y + 0.5f - dy0) * yscale;
        int sy = clampi((int)floorf(v), isy0, isy1 - 1);
        const uint32_t *srow = src + (size_t)sy * (size_t)src_w;
        uint32_t *drow = dst->pixels + (size_t)y * (size_t)dst->stride + (size_t)cx0;

        if (copy_rows && copy_base >= isx0 && copy_base + copy_w <= isx1) {
            memcpy(drow, srow + copy_base, (size_t)copy_w * sizeof(uint32_t));
            continue;
        }

        int64_t fx = fx0;
        if (src_opaque && alpha == 255) {
            for (int i = 0; i < copy_w; i++, fx += xstep) {
                int sx = clampi((int)(fx >> 16), isx0, isx1 - 1);
                drow[i] = srow[sx];
            }
        } else if (alpha == 255) {
            for (int i = 0; i < copy_w; i++, fx += xstep) {
                int sx = clampi((int)(fx >> 16), isx0, isx1 - 1);
                uint32_t s = srow[sx];
                uint32_t sa = s >> 24;
                if (sa == 255)     drow[i] = s;
                else if (sa != 0)  drow[i] = over_pm(s, drow[i]);
            }
        } else {
            for (int i = 0; i < copy_w; i++, fx += xstep) {
                int sx = clampi((int)(fx >> 16), isx0, isx1 - 1);
                uint32_t s = scale_pm(srow[sx], (uint32_t)alpha);
                uint32_t sa = s >> 24;
                if (sa == 255)     drow[i] = s;
                else if (sa != 0)  drow[i] = over_pm(s, drow[i]);
            }
        }
    }
}

/* ----------------------------------------------------------------- fill */

void tl_blit_fill(const tl_blit_target *dst, float x0f, float y0f, float x1f, float y1f,
                  uint32_t argb)
{
    if (!dst || !dst->pixels) return;
    uint32_t a = argb >> 24;
    if (a == 0) return;
    if (!(x1f > x0f) || !(y1f > y0f)) return;

    int x0 = span_begin(x0f), x1 = span_end(x1f);
    int y0 = span_begin(y0f), y1 = span_end(y1f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->width)  x1 = dst->width;
    if (y1 > dst->height) y1 = dst->height;
    if (x0 >= x1 || y0 >= y1) return;

    /* Android's 0xAARRGGBB, premultiplied, into this layout's red-lowest bytes. */
    uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    uint32_t s = scale_pm(r | (g << 8) | (b << 16), a) | (a << 24);

    for (int y = y0; y < y1; y++) {
        uint32_t *row = dst->pixels + (size_t)y * (size_t)dst->stride;
        if (a == 255) {
            for (int x = x0; x < x1; x++) row[x] = s;
        } else {
            for (int x = x0; x < x1; x++) row[x] = over_pm(s, row[x]);
        }
    }
}

bool tl_blit_image_is_opaque(const uint32_t *pixels, int w, int h)
{
    if (!pixels || w <= 0 || h <= 0) return false;
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        if ((pixels[i] >> 24) != 255) return false;
    }
    return true;
}
