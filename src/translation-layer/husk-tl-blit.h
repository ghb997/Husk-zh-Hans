/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A software blitter for the canvas: nearest-neighbour sprite draws and
 * rectangle fills, straight into the framebuffer.
 *
 * Why not CoreGraphics. It is what drew everything, and it is where the whole
 * frame went: the game's own logic costs microseconds, and the canvas draws cost
 * two to four milliseconds -- a hundred times more -- because CoreGraphics pays
 * a fixed price per call (state save and restore, a transform, colour
 * management, an interpolation mode) on images that are a few kilobytes of flat
 * pixel art. It also gets two things wrong for this kind of content. It
 * anti-aliases the edges of a rectangle that lands between pixels, so two
 * scrolling tiles that meet on a fractional edge each cover it partially and let
 * the background show through as a seam; and it filters a sprite that sits at a
 * fractional position, so a scrolling pipe's edges shimmer.
 *
 * What the game asked for is what Android's Skia does without a filter: a
 * destination pixel is covered if its centre is inside the destination
 * rectangle, and takes the source pixel that centre lands on. That is all this
 * does. Two rectangles that share an edge therefore share it exactly, however
 * fractional it is.
 *
 * Everything is premultiplied RGBA with the bytes in that memory order -- red
 * lowest -- the layout of the framebuffer and of every decoded bitmap.
 */
#ifndef HUSK_TL_BLIT_H
#define HUSK_TL_BLIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_blit_target {
    uint32_t *pixels;
    int width, height;
    int stride;                 /* in pixels, not bytes */
} tl_blit_target;

/*
 * Draw the source rectangle (sx0,sy0)-(sx1,sy1) -- in source pixels, rounded
 * outward to whole pixels -- into the destination rectangle (dx0,dy0)-(dx1,dy1),
 * in target pixels with y down. Compositing is source-over with `alpha` (0-255)
 * applied to the source. `src_opaque` promises that every source pixel has full
 * alpha, which lets an unscaled draw be a row copy. Anything outside the target
 * is clipped. A rectangle with no area draws nothing.
 */
void tl_blit_draw(const tl_blit_target *dst,
                  const uint32_t *src, int src_w, int src_h, bool src_opaque,
                  float sx0, float sy0, float sx1, float sy1,
                  float dx0, float dy0, float dx1, float dy1,
                  int alpha);

/*
 * Fill (x0,y0)-(x1,y1) with a colour, source-over. `argb` is Android's own
 * 0xAARRGGBB and is NOT premultiplied; this does that.
 */
void tl_blit_fill(const tl_blit_target *dst,
                  float x0, float y0, float x1, float y1,
                  uint32_t argb);

/* Whether every pixel of an image is fully opaque. Scans once; callers cache it. */
bool tl_blit_image_is_opaque(const uint32_t *pixels, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_BLIT_H */
