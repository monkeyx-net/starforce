#ifndef STARFORCE_VIDEO_H
#define STARFORCE_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx/enhance.h"
#include "gfx/fb.h"

/* Fullscreen-desktop window (windowed if `windowed`), vsync, nearest scaling. */
bool video_init(bool windowed);
/* Device pixels per arcade pixel worth rendering at (>= 1): the integer
   scale shown, or for a fitted picture the next integer above the fit. */
int video_display_scale(void);
/* Upload the framebuffer (any scale) and present it at the largest integer
   scale, or (when that would leave much of the screen unused, as on
   640x480) fitted to the screen, pillarboxed; with the glow's grid (if not
   NULL) stretched smoothly and added over it, and CRT scanlines over that
   if `scanlines` (down the screen: the monitor is turned). */
void video_present(const Framebuffer *fb, bool scanlines, const uint32_t *glow);
/* The GPU can add the glow (enhance_glow_grid, passed to video_present):
   an accelerated renderer; else the CPU adds it to the framebuffer. */
bool video_gpu_glow(void);
void video_shutdown(void);

#endif
