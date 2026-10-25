/*
 * Pixel-art upscalers for the enhanced graphics modes, on ARGB8888 pixels:
 *   SCALER_CRISP   Scale2x / Scale3x (EPX, AdvMAME): hard pixels, rounded steps
 *   SCALER_SMOOTH  2xBR / 3xBR (Hyllian's xBR): blended, anti-aliased edges
 * 4x runs the 2x pass twice.
 * Blending is done on premultiplied alpha, so sprite edges fade to transparent.
 */
#ifndef STARFORCE_SCALE_H
#define STARFORCE_SCALE_H

#include <stdint.h>

typedef enum { SCALER_CRISP, SCALER_SMOOTH } Scaler;

/* Upscale w x h pixels (row stride sstride) by factor 2..4 into dst (row stride
   dstride). Outside the source, pixels repeat the edge if `clamp`, else are
   transparent (sprites). Returns 0, or -1 when out of memory. */
int scale_pixels(const uint32_t *src, int w, int h, int sstride, uint32_t *dst, int dstride,
                 int factor, Scaler scaler, int clamp);

#endif
