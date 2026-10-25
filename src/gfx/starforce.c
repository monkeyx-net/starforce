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

/* ---- a frame, a native row at a time, back to front ---------------------------- */

typedef struct {
    int top, left, size, attr, code;
    uint16_t colour;
} Spr;

/* the sprites of priority `prio` crossing row y onto it, the first on top */
static void sprite_row(const SfGfx *g, const Spr *spr, int prio, int y, uint16_t *line)
{
    for (int i = 31; i >= 0; i--) {
        const Spr *s = &spr[i];
        int py = (y - s->top) & 255;
        if ((s->attr >> 4 & 3) != prio || py >= s->size) continue;
        int v = s->attr & 0x80 ? s->size - 1 - py : py;
        for (int px = 0; px < s->size; px++) {
            int u = s->attr & 0x40 ? s->size - 1 - px : px;
            int unit = s->size == 32 ? (s->code & 0x7F) * 4 + (v >> 4) * 2 + (u >> 4) : s->code;
            int pen = g->spr[unit][(v & 15) * 16 + (u & 15)];
            if (pen) line[(s->left + px) & 255] = (uint16_t)(s->colour + pen);
        }
    }
}

/* a bg layer's row y onto it; flipped, its scroll across goes the other way
   (MAME's) */
static void bg_row(const uint8_t (*tiles)[256], const uint8_t *ram, int layer, int base, int y, int sx, int sy, int flip,
                   uint16_t *line)
{
    int my = (y + sy) & 2047, k = flip ? -sx : sx;
    const uint8_t *row = ram + (my >> 4) * 16, *pens = NULL;
    int ty = (my & 15) * 16, colour = 0;
    for (int x = 0; x < 256; x++) {
        int mx = (x + k) & 255;
        if (!x || !(mx & 15)) {
            int code = row[mx >> 4];
            colour = base + (layer == 1 ? (code >> 4 & 6) | code >> 7 : code >> 5) * 8;
            pens = tiles[layer == 3 ? code & 0x7F : code] + ty;
        }
        int pen = pens[mx & 15];
        if (pen) line[x] = (uint16_t)(colour + pen);
    }
}

/* the fg layer's row y onto it, each column scrolled */
static void fg_row(const SfGfx *g, const SfFrame *f, int y, uint16_t *line)
{
    for (int col = 0; col < 32; col++) {
        int my = (y + f->regs[col]) & 255;
        int cell = (my >> 3) * 32 + col, attr = f->fgcolor[cell];
        int code = f->fgvideo[cell] | (attr & 0x10) << 4;
        const uint8_t *pens = g->fg[code] + (attr & 0x80 ? 7 - (my & 7) : my & 7) * 8;
        uint16_t *out = line + col * 8;
        int colour = (attr & 7) * 8;
        for (int x = 0; x < 8; x++)
            if (pens[x]) out[x] = (uint16_t)(colour + pens[x]);
    }
}

void sf_draw(const SfGfx *g, const SfFrame *f, uint32_t *screen)
{
    uint32_t pal[512];
    for (int i = 0; i < 512; i++) pal[i] = sf_colour(f->palette[i]);
    Spr spr[32];
    for (int i = 0; i < 32; i++) {
        const uint8_t *s = f->sprites + i * 4;
        spr[i].size = s[0] >= 0xC0 ? 32 : 16;
        spr[i].code = s[0];
        spr[i].attr = s[1];
        spr[i].top = (256 - s[2] - spr[i].size + (f->flip ? -2 : 0)) & 255;
        spr[i].left = s[3];
        spr[i].colour = (uint16_t)(320 + (s[1] & 7) * 8);
    }
    const uint8_t *r = f->regs;
    int sy1 = r[0x30] | r[0x31] << 8, sx1 = r[0x35];
    int sy3 = r[0x20] | r[0x21] << 8, sx3 = r[0x25];
    int stripes = r[0x27];
    static uint16_t native[224][256]; /* native rows 16-239 */
    uint16_t back[256]; /* the backdrop, or the stripes: the same on every row */
    for (int x = 0; x < 256; x++)
        back[x] = stripes == 0xFF ? 0 : (uint16_t)(384 + (x * 16 / (stripes < 15 ? 16 : stripes + 1) & 15));
    for (int y = 16; y < 240; y++) {
        uint16_t *line = native[y - 16];
        memcpy(line, back, sizeof back);
        sprite_row(g, spr, 0, y, line);
        bg_row(g->bg3, f->bg3, 3, 192, y, sx3, sy3, f->flip, line);
        sprite_row(g, spr, 1, y, line);
        bg_row(g->bg2, f->bg2, 2, 128, y, sx1, sy1, f->flip, line);
        sprite_row(g, spr, 2, y, line);
        bg_row(g->bg1, f->bg1, 1, 64, y, sx1, sy1, f->flip, line);
        sprite_row(g, spr, 3, y, line);
        fg_row(g, f, y, line);
    }
    /* turned: screen x = 239 - native y, screen y = native x; flipped, the
       whole native picture turned round; in blocks, to stay in the cache */
    for (int by = 0; by < SF_H; by += 16)
        for (int bx = 0; bx < SF_W; bx += 16)
            for (int sy = by; sy < by + 16; sy++) {
                uint32_t *out = screen + sy * SF_W;
                if (f->flip)
                    for (int sx = bx; sx < bx + 16; sx++) out[sx] = pal[native[sx][255 - sy]];
                else
                    for (int sx = bx; sx < bx + 16; sx++) out[sx] = pal[native[223 - sx][sy]];
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
