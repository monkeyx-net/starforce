#include "gfx/starforce.h"

#include <string.h>

#include "emu/board.h"

/* ---- the ROM's graphics ------------------------------------------------------------ */

/* Three bitplanes, a third of the region each, the last third bit 0; a tile's
   rows top to bottom, each byte's bit 7 its left pixel. 16x16 tiles are four
   8x8 quarters: top left, top right, bottom left, bottom right. */
static void decode(uint8_t *out, const uint8_t *region, unsigned size, int unit, int w)
{
    unsigned plane = size / 3;
    for (int py = 0; py < w; py++)
        for (int px = 0; px < w; px++) {
            int q = w == 16 ? (py >> 3) * 2 + (px >> 3) : 0;
            unsigned at = (unsigned)unit * (unsigned)(w * w / 8) + (unsigned)q * 8 + (unsigned)(py & 7);
            int bit = 7 - (px & 7), pen = 0;
            for (int k = 0; k < 3; k++) pen |= (region[(2 - k) * plane + at] >> bit & 1) << k;
            out[py * w + px] = (uint8_t)pen;
        }
}

void sf_gfx_from_rom(SfGfx *g, const RomSet *rom)
{
    for (int t = 0; t < SF_FG_TILES; t++) decode(g->fg[t], rom->fgtiles, ROM_FGTILES_SIZE, t, 8);
    for (int t = 0; t < SF_BG_TILES; t++) {
        decode(g->bg1[t], rom->bgtiles1, ROM_BGTILES1_SIZE, t, 16);
        decode(g->bg2[t], rom->bgtiles2, ROM_BGTILES2_SIZE, t, 16);
    }
    for (int t = 0; t < SF_BG3_TILES; t++) decode(g->bg3[t], rom->bgtiles3, ROM_BGTILES3_SIZE, t, 16);
    for (int t = 0; t < SF_SPRITES; t++) decode(g->spr[t], rom->sprites, ROM_SPRITES_SIZE, t, 16);
}

uint32_t sf_colour(uint8_t v)
{
    int i = v >> 6;
    int c[3] = { v & 3, v >> 2 & 3, v >> 4 & 3 };
    uint32_t rgb = 0;
    for (int k = 0; k < 3; k++) rgb = rgb << 8 | (uint32_t)(c[k] ? (c[k] << 2 | i) * 17 : 0);
    return 0xFF000000u | rgb;
}

/* ---- a frame ----------------------------------------------------------------------- */

#define NONE 0xFFFF

/* the sprites of one priority, in native coordinates: palette index or NONE */
static uint16_t spr_layer[4][256][256];

static void draw_sprites(const SfGfx *g, const SfFrame *f)
{
    for (int p = 0; p < 4; p++)
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) spr_layer[p][y][x] = NONE;
    for (int i = 31; i >= 0; i--) { /* the first on top */
        const uint8_t *s = f->sprites + i * 4;
        int big = s[0] >= 0xC0, size = big ? 32 : 16;
        int attr = s[1], prio = attr >> 4 & 3;
        int top = 256 - s[2] - size + (f->flip ? -2 : 0), left = s[3];
        uint16_t colour = (uint16_t)(320 + (attr & 7) * 8);
        for (int py = 0; py < size; py++)
            for (int px = 0; px < size; px++) {
                int u = attr & 0x40 ? size - 1 - px : px, v = attr & 0x80 ? size - 1 - py : py;
                int unit = big ? (s[0] & 0x7F) * 4 + (v >> 4) * 2 + (u >> 4) : s[0];
                int pen = g->spr[unit][(v & 15) * 16 + (u & 15)];
                if (pen) spr_layer[prio][(top + py) & 255][(left + px) & 255] = (uint16_t)(colour + pen);
            }
    }
}

/* a bg layer's pen at native (x, y) before the flip, or 0; flipped, its
   scroll across goes the other way (MAME's) */
static int bg_pixel(const uint8_t (*tiles)[256], const uint8_t *ram, int layer, int x, int y, int sx, int sy, int flip,
                    int *colour)
{
    int mx = (flip ? x - sx : x + sx) & 255, my = (y + sy) & 2047;
    int code = ram[(my >> 4) * 16 + (mx >> 4)];
    int c = layer == 1 ? (code >> 4 & 6) | code >> 7 : code >> 5;
    int tile = layer == 3 ? code & 0x7F : code;
    *colour = c;
    return tiles[tile][(my & 15) * 16 + (mx & 15)];
}

void sf_draw(const SfGfx *g, const SfFrame *f, uint32_t *screen)
{
    static uint16_t native[256][256];
    uint32_t pal[512];
    for (int i = 0; i < 512; i++) pal[i] = sf_colour(f->palette[i]);
    draw_sprites(g, f);
    const uint8_t *r = f->regs;
    int sy1 = r[0x30] | r[0x31] << 8, sx1 = r[0x35];
    int sy3 = r[0x20] | r[0x21] << 8, sx3 = r[0x25];
    int stripes = r[0x27];
    for (int y = 16; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            int c, pen;
            uint16_t v = 0; /* the backdrop */
            if (stripes != 0xFF) {
                int w = stripes < 15 ? 16 : stripes + 1;
                v = (uint16_t)(384 + (x * 16 / w & 15));
            }
            if (spr_layer[0][y][x] != NONE) v = spr_layer[0][y][x];
            if ((pen = bg_pixel(g->bg3, f->bg3, 3, x, y, sx3, sy3, f->flip, &c))) v = (uint16_t)(192 + c * 8 + pen);
            if (spr_layer[1][y][x] != NONE) v = spr_layer[1][y][x];
            if ((pen = bg_pixel(g->bg2, f->bg2, 2, x, y, sx1, sy1, f->flip, &c))) v = (uint16_t)(128 + c * 8 + pen);
            if (spr_layer[2][y][x] != NONE) v = spr_layer[2][y][x];
            if ((pen = bg_pixel(g->bg1, f->bg1, 1, x, y, sx1, sy1, f->flip, &c))) v = (uint16_t)(64 + c * 8 + pen);
            if (spr_layer[3][y][x] != NONE) v = spr_layer[3][y][x];
            int col = x >> 3, my = (y + f->regs[col]) & 255;
            int cell = (my >> 3) * 32 + col, attr = f->fgcolor[cell];
            int code = f->fgvideo[cell] | (attr & 0x10) << 4;
            int py = attr & 0x80 ? 7 - (my & 7) : my & 7;
            if ((pen = g->fg[code][py * 8 + (x & 7)])) v = (uint16_t)((attr & 7) * 8 + pen);
            native[y][x] = v;
        }
    /* turned: screen x = 239 - native y, screen y = native x; flipped, the
       whole native picture turned round */
    for (int sy = 0; sy < SF_H; sy++)
        for (int sx = 0; sx < SF_W; sx++) {
            int nx = sy, ny = 239 - sx;
            if (f->flip) nx = 255 - nx, ny = 255 - ny;
            screen[sy * SF_W + sx] = pal[native[ny][nx]];
        }
}

void sf_frame_from_board(SfFrame *f, const Board *b)
{
    f->fgvideo = b->fgvideo;
    f->fgcolor = b->fgcolor;
    f->sprites = b->sprites;
    f->palette = b->palette;
    f->regs = b->regs;
    f->bg1 = b->bg1;
    f->bg2 = b->bg2;
    f->bg3 = b->bg3;
    f->flip = b->flip;
}
