/*
 * A 3x5 debug font drawn from patterns in this file (not from the ROM), for
 * the test card and diagnostics. The game's own font will come from the ROM.
 * Letters are uppercased; unknown characters draw as '?'.
 */
#ifndef STARFORCE_DEBUGFONT_H
#define STARFORCE_DEBUGFONT_H

#include <stdint.h>
#include "gfx/fb.h"

#define DEBUG_GLYPH_W 3
#define DEBUG_GLYPH_H 5
#define DEBUG_ADVANCE 4 /* glyph plus 1 px gap */

/* Draw text with its top-left at (x, y) in arcade pixels, colour 0xRRGGBB. */
void debug_text(Framebuffer *fb, double x, double y, const char *text, uint32_t rgb);
int debug_text_width(const char *text);

#endif
