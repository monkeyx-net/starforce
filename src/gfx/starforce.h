/*
 * The Star Force picture, as the player sees it: 224x256 portrait (the
 * board's 256x224 turned 90 degrees). A frame is drawn from the video RAM,
 * the sprite RAM, the palette RAM and the registers, in the board's own
 * (native) coordinates, then turned: screen x = 239 - native y, screen y =
 * native x.
 *
 * Back to front: the backdrop (palette 0), the stripes, the sprites of
 * priority 0, bg3, sprites 1, bg2, sprites 2, bg1, sprites 3, the fg layer.
 * - fg: 32x32 tiles of 8x8 ($9000 codes, $9400 colours: bits 0-2 colour,
 *   bit 4 the code's ninth bit, bit 7 flips the tile top to bottom), each
 *   column scrolled up by its byte at $9E00; palette colour x 8 + pen.
 * - bg1, bg2, bg3: 16x128 tiles of 16x16 (256 x 2048 pixels), a byte a
 *   tile: bg2 and bg3 colour bits 5-7, bg1 bit 7 then 5 and 6; bg3's tile
 *   the low 7 bits. bg1 and bg2 scroll by $9E30-$9E31 (down) and $9E35
 *   (across), bg3 by $9E20-$9E21 and $9E25; palettes 64, 128, 192.
 * - sprites: 32 of 4 bytes at $9800 (code; colour bits 0-2, priority bits
 *   4-5, flips bit 6 across and 7 down; 256 - y; x), 16x16, codes $C0 up
 *   32x32 (four 16x16 from (code & $7F) x 4); the first on top; palette 320.
 * - stripes: unless $9E27 is $FF, palette 384-399 in stripes across, each
 *   ($9E27 + 1) / 16 pixels wide.
 * - the palette: a byte a colour, 2 bits each of red, green, blue and a
 *   shared intensity: (c x 4 + i) x 17, 0 where c is 0.
 * - $D000 set turns the picture round (the sprites 2 pixels further down;
 *   the bg layers' scroll across, $9E35 and $9E25, the other way).
 * The graphics come decoded (sf_gfx_from_rom, or the extracted assets), a
 * pen per pixel in native order.
 */
#ifndef STARFORCE_GFX_STARFORCE_H
#define STARFORCE_GFX_STARFORCE_H

#include <stdint.h>

#include "assets/rom.h"

#define SF_W 224
#define SF_H 256

#define SF_FG_TILES 512
#define SF_BG_TILES 256  /* bg1 and bg2; bg3 has 128 */
#define SF_BG3_TILES 128
#define SF_SPRITES 512   /* 16x16 units */

typedef struct {
    uint8_t fg[SF_FG_TILES][64];
    uint8_t bg1[SF_BG_TILES][256];
    uint8_t bg2[SF_BG_TILES][256];
    uint8_t bg3[SF_BG3_TILES][256];
    uint8_t spr[SF_SPRITES][256];
} SfGfx;

/* What a frame is drawn from */
typedef struct {
    const uint8_t *fgvideo, *fgcolor; /* 0x400 each */
    const uint8_t *sprites;           /* 0x80 */
    const uint8_t *palette;           /* 0x200 */
    const uint8_t *regs;              /* 0x40, $9E00 */
    const uint8_t *bg1, *bg2, *bg3;   /* 0x800 each */
    int flip;
} SfFrame;

/* Decode the ROM set's graphics. */
void sf_gfx_from_rom(SfGfx *g, const RomSet *rom);
/* A palette byte as 0xRRGGBB */
uint32_t sf_colour(uint8_t v);
/* Draw a frame into screen (SF_W x SF_H, 0xFFRRGGBB, row-major). */
void sf_draw(const SfGfx *g, const SfFrame *f, uint32_t *screen);

struct Board;
/* The frame the emulated board's memory holds */
void sf_frame_from_board(SfFrame *f, const struct Board *b);

#endif
