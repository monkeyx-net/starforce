#ifndef STARFORCE_VIDEO_H
#define STARFORCE_VIDEO_H

#include <stdbool.h>
#include "gfx/fb.h"

/* Fullscreen-desktop window (windowed if `windowed`), vsync, nearest scaling. */
bool video_init(bool windowed);
/* Device pixels per arcade pixel worth rendering at (>= 1): the integer
   scale shown, or for a fitted picture the next integer above the fit. */
int video_display_scale(void);
/* Upload the framebuffer (any scale) and present it at the largest integer
   scale, or (when that would leave much of the screen unused, as on
   640x480) fitted to the screen, pillarboxed; with CRT scanlines over it
   if `scanlines` (down the screen: the monitor is turned). */
void video_present(const Framebuffer *fb, bool scanlines);
void video_shutdown(void);

#endif
