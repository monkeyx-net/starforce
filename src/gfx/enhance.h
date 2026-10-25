/*
 * Enhanced graphics (not in the arcade; classic is the default):
 *   FILTER  the arcade frame is drawn as usual, then upscaled whole (Scale2x/3x
 *           or xBR) to the display's integer scale
 *   HD      everything is drawn at that scale: sprites and fonts from upscaled
 *           copies, shapes and effects at device-pixel precision
 * plus a glow (bloom) pass and CRT scanlines (the scanlines are drawn by the
 * platform layer). The sim is untouched, so play is identical in every mode.
 */
#ifndef STARFORCE_ENHANCE_H
#define STARFORCE_ENHANCE_H

#include "assets/png.h"
#include "gfx/fb.h"
#include "gfx/scale.h"

typedef enum { GFX_CLASSIC, GFX_FILTER, GFX_HD } GfxMode;

typedef struct {
    int mode;      /* GfxMode */
    int smooth;    /* xBR instead of Scale2x/3x */
    int glow;
    int scanlines;
} GfxSettings;

/* "CLASSIC", "HD SMOOTH + GLOW", ... for the on-screen notice. */
const char *gfx_describe(const GfxSettings *g);
/* Step through the presets (classic, crisp, smooth, hd-crisp, hd-smooth, hd-glow, ...). */
void gfx_next_preset(GfxSettings *g);
/* A preset by its command-line name; returns 0, or -1 if unknown. */
int gfx_parse(const char *name, GfxSettings *g);
/* Preset names for --help, space separated. */
const char *gfx_preset_names(void);

/* Arcade pixels drawn per device pixel (1 = classic, else 2 or 3) given the
   display's integer scale: 4 and up render at 2, doubled by the GPU. */
int gfx_render_scale(const GfxSettings *g, int display_scale);

typedef struct {
    int x, y, w, h;
} EnhanceRect;

/* Register an image whose frames (rects, copied) are upscaled one by one for
   HD rendering; `crisp` keeps the crisp scaler whatever the preset (text:
   xBR rounds small glyphs into blobs). Returns 0, or -1 when full. */
int enhance_add_image(const Image *img, const EnhanceRect *rects, int count, int crisp);
void enhance_clear_images(void);
/* Build the upscaled copies of every registered image for HD rendering at
   `factor` (cached; rebuilt when factor or scaler change). Returns 0, or -1
   when out of memory. */
int enhance_prepare(int factor, Scaler scaler);

/* FILTER: upscale the arcade frame src (scale 1) into dst (scale 2..4). */
int enhance_filter_frame(const Framebuffer *src, Framebuffer *dst, Scaler scaler);
/* Bloom: add a blurred copy of the frame onto itself. */
void enhance_glow(Framebuffer *fb);

/* The glow's blurred frame alone, for the GPU to stretch (linear) and add
   over fb: ENHANCE_GLOW_W x ENHANCE_GLOW_H, opaque-free packed RGB, each
   entry the middle of a cell of 2 x 2 arcade pixels (enhance_glow's own
   taps). Valid until the next call. */
#define ENHANCE_GLOW_W (FB_WIDTH / 2)
#define ENHANCE_GLOW_H (FB_HEIGHT / 2)
const uint32_t *enhance_glow_grid(const Framebuffer *fb);

#endif
