/*
 * The picture drawn from MAME's memory against MAME's own (label mame):
 *   test_capture <starforc.zip|folder> <frame_n.txt> <n.png> <n>
 * Reads what capture_probe.lua dumped at a frame (the video RAM, sprites,
 * palette and registers, the flip), draws it with gfx/starforce.c from the
 * ROM set's graphics and compares it with MAME's snapshot pixel for pixel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/png.h"
#include "assets/rom.h"
#include "gfx/starforce.h"

static RomSet rom;
static SfGfx gfx;
static uint8_t fgvideo[0x400], fgcolor[0x400], sprites[0x80], palette[0x200], regs[0x40], bg1[0x800], bg2[0x800], bg3[0x800];

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int hex_bytes(const char *p, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        int hi = hex(p[2 * i]), lo = hex(p[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: test_capture <rom> <frame_n.txt> <n.png> <n>\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    sf_gfx_from_rom(&gfx, &rom);
    FILE *f = fopen(argv[2], "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    static const struct {
        const char *name;
        uint8_t *to;
        int len;
    } parts[] = {
        { "fgvideo", fgvideo, 0x400 }, { "fgcolor", fgcolor, 0x400 }, { "sprites", sprites, 0x80 },
        { "palette", palette, 0x200 }, { "regs", regs, 0x40 },        { "bg1", bg1, 0x800 },
        { "bg2", bg2, 0x800 },         { "bg3", bg3, 0x800 },
    };
    static char text[0x3000 * 2 + 64];
    int have = 0, flip = 0;
    while (fgets(text, sizeof text, f)) {
        for (size_t k = 0; k < sizeof parts / sizeof parts[0]; k++) {
            size_t n = strlen(parts[k].name);
            if (!strncmp(text, parts[k].name, n) && text[n] == ' ' && !hex_bytes(text + n + 1, parts[k].to, parts[k].len)) have++;
        }
        if (!strncmp(text, "flip ", 5) && hex(text[6]) >= 0) flip = hex(text[6]) & 1;
    }
    fclose(f);
    if (have != (int)(sizeof parts / sizeof parts[0])) {
        fprintf(stderr, "%s: incomplete\n", argv[2]);
        return 1;
    }
    SfFrame fr = { fgvideo, fgcolor, sprites, palette, regs, bg1, bg2, bg3, flip };
    static uint32_t screen[SF_H * SF_W];
    sf_draw(&gfx, &fr, screen);
    Image shot;
    const char *perr;
    if (png_load(argv[3], &shot, &perr) || shot.width != SF_W || shot.height != SF_H) {
        fprintf(stderr, "%s: not a %dx%d snapshot\n", argv[3], SF_W, SF_H);
        return 1;
    }
    long diff = 0;
    for (int y = 0; y < SF_H; y++)
        for (int x = 0; x < SF_W; x++) {
            const uint8_t *p = shot.rgba + ((size_t)y * SF_W + (size_t)x) * 4;
            uint32_t want = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            uint32_t got = screen[y * SF_W + x] & 0xFFFFFF;
            if (got != want && diff++ < 4) printf("  pixel %d,%d: %06X, MAME %06X\n", x, y, (unsigned)got, (unsigned)want);
        }
    image_free(&shot);
    const char *dump = getenv("DUMP_DIR");
    if (dump && *dump) {
        static uint8_t rgba[SF_W * SF_H * 4];
        for (int i = 0; i < SF_W * SF_H; i++) {
            rgba[i * 4] = (uint8_t)(screen[i] >> 16), rgba[i * 4 + 1] = (uint8_t)(screen[i] >> 8);
            rgba[i * 4 + 2] = (uint8_t)screen[i], rgba[i * 4 + 3] = 0xFF;
        }
        char out[1100];
        snprintf(out, sizeof out, "%s/drawn_%s.png", dump, argv[4]);
        png_write(out, rgba, SF_W, SF_H);
    }
    printf("frame %s: %ld pixels differ\n", argv[4], diff);
    return diff != 0;
}
