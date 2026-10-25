/*
 * Enhanced graphics (no ROM needed): the upscalers, the presets, blits at
 * scale 2 against scale 1, and the glow pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/blit.h"
#include "gfx/enhance.h"
#include "gfx/scale.h"

static int failures;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

#define RED 0xFFFF0000u
#define BLUE 0xFF0000FFu

static void flat_images_stay_flat(void)
{
    uint32_t in[5 * 4], out[5 * 4 * 16];
    for (int i = 0; i < 20; i++) in[i] = RED;
    for (int sc = 0; sc < 2; sc++)
        for (int f = 2; f <= 4; f++) {
            memset(out, 0, sizeof out);
            CHECK(!scale_pixels(in, 5, 4, 5, out, 5 * f, f, (Scaler)sc, 1), "scale_pixels failed");
            int bad = 0;
            for (int i = 0; i < 20 * f * f; i++) bad += out[i] != RED;
            CHECK(!bad, "scaler %d at %dx: %d pixels of a flat image changed", sc, f, bad);
        }
}

static void lone_pixel(void)
{
    /* one opaque pixel, transparent around it: Scale2x / Scale3x keep the
       f x f block; the rest may round it but keep its centre, its colour and
       its symmetry */
    uint32_t in[1] = { BLUE }, out[16];
    for (int sc = 0; sc < 2; sc++)
        for (int f = 2; f <= 4; f++) {
            memset(out, 0xAB, sizeof out);
            scale_pixels(in, 1, 1, 1, out, f, f, (Scaler)sc, 0);
            int bad = 0;
            for (int y = 0; y < f; y++)
                for (int x = 0; x < f; x++) {
                    uint32_t c = out[y * f + x];
                    if (sc == SCALER_CRISP && f < 4) bad += c != BLUE;
                    bad += c && (c & 0xFFFFFF) != 0x0000FF;
                    bad += c != out[y * f + (f - 1 - x)] || c != out[(f - 1 - y) * f + x];
                }
            CHECK(!bad, "scaler %d at %dx: lone pixel has %d bad pixels", sc, f, bad);
            CHECK(out[(f / 2) * f + f / 2] >> 24, "scaler %d at %dx: centre cleared", sc, f);
        }
}

/* A staircase (0 = clear, 1 = red):
     1 0 0
     1 1 0
     1 1 1    */
static const int STAIRS[3][3] = { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 } };

static void staircase(void)
{
    uint32_t in[9], out[9 * 4];
    for (int i = 0; i < 9; i++) in[i] = STAIRS[i / 3][i % 3] ? RED : 0;

    /* Scale2x (EPX) cuts the step: the middle pixel's top-right quarter clears */
    scale_pixels(in, 3, 3, 3, out, 6, 2, SCALER_CRISP, 0);
    CHECK(out[2 * 6 + 3] == 0, "Scale2x: top-right of the middle pixel is %08X, want clear", out[2 * 6 + 3]);
    CHECK(out[3 * 6 + 2] == RED, "Scale2x: bottom-left of the middle pixel is %08X, want red", out[3 * 6 + 2]);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++) {
            uint32_t c = out[y * 6 + x];
            CHECK(c == 0 || c == RED, "Scale2x made a new colour %08X at %d,%d", c, x, y);
        }

    /* 2xBR blends along the diagonal: partly transparent red, never a new hue */
    scale_pixels(in, 3, 3, 3, out, 6, 2, SCALER_SMOOTH, 0);
    int blended = 0;
    for (int i = 0; i < 36; i++) {
        uint32_t c = out[i], a = c >> 24;
        if (a > 0 && a < 255) blended++;
        CHECK(!a || (c & 0xFFFFFF) == 0xFF0000, "2xBR changed the hue: %08X", c);
    }
    CHECK(blended > 0, "2xBR left the staircase unblended");
}

static void presets(void)
{
    GfxSettings g = { 0 }, start = g;
    CHECK(!strcmp(gfx_describe(&g), "CLASSIC"), "default is %s", gfx_describe(&g));
    int steps = 0;
    do {
        gfx_next_preset(&g);
        steps++;
    } while (memcmp(&g, &start, sizeof g) && steps < 50);
    CHECK(steps == 8, "presets cycle back to classic after %d steps, want 8", steps);

    CHECK(!gfx_parse("hd-glow-crt", &g) && g.mode == GFX_HD && g.smooth && g.glow && g.scanlines, "hd-glow-crt");
    CHECK(!strcmp(gfx_describe(&g), "HD SMOOTH + GLOW + CRT"), "describe: %s", gfx_describe(&g));
    CHECK(gfx_parse("nope", &g) == -1, "unknown preset accepted");
    CHECK(gfx_render_scale(&g, 1) == 2 && gfx_render_scale(&g, 3) == 3 && gfx_render_scale(&g, 9) == FB_MAX_SCALE,
          "render scale");
    gfx_parse("classic-crt", &g);
    CHECK(gfx_render_scale(&g, 4) == 1, "classic renders at 1");
}

/* With no upscaled copy registered, a scale-2 frame is the scale-1 frame doubled. */
static void scale2_matches_scale1(void)
{
    static uint8_t px[6 * 5 * 4];
    for (int i = 0; i < 6 * 5; i++) {
        px[i * 4] = (uint8_t)(i * 40);
        px[i * 4 + 1] = (uint8_t)(255 - i * 8);
        px[i * 4 + 2] = (uint8_t)(i * 3);
        px[i * 4 + 3] = i % 7 ? 255 : 0;
    }
    Image img = { 6, 5, px };
    Framebuffer f1 = { 0 }, f2 = { 0 };
    CHECK(!fb_init(&f1, 1) && !fb_init(&f2, 2), "fb_init");
    for (int k = 0; k < 2; k++) {
        Framebuffer *f = k ? &f2 : &f1;
        blit(f, &img, 0, 0, 6, 5, 10, 20, 1, 0xFFFFFF);
        blit(f, &img, 1, 1, 4, 3, 100, 30, 3, 0x80FF40);
        fill_rectf(f, 50, 60, 7, 3, 0x123456);
        fb_fill_rect(f, 200, 200, 4, 4, 0xFF00FF00u);
    }
    int bad = 0;
    for (int y = 0; y < FB_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            uint32_t c = fb_row(&f1, y)[x];
            for (int k = 0; k < 4; k++) bad += fb_row(&f2, y * 2 + k / 2)[x * 2 + k % 2] != c;
        }
    CHECK(!bad, "scale-2 frame differs from the doubled scale-1 frame in %d pixels", bad);
    fb_free(&f1);
    fb_free(&f2);
}

static void glow(void)
{
    Framebuffer f = { 0 };
    CHECK(!fb_init(&f, 2), "fb_init");
    enhance_glow(&f);
    int lit = 0;
    for (long i = 0; i < (long)f.w * f.h; i++) lit += f.px[i] != 0xFF000000u;
    CHECK(!lit, "glow lit %d pixels of a black frame", lit);

    fb_fill_rect(&f, 140, 120, 4, 4, 0xFFFFFFFFu);
    enhance_glow(&f);
    uint32_t near = fb_row(&f, 120 * 2 + 4)[136 * 2], far = fb_row(&f, 20)[20];
    CHECK(near != 0xFF000000u && (near & 0xFF) < 0xFF, "no glow next to a white square: %08X", near);
    CHECK(far == 0xFF000000u, "glow reached far corner: %08X", far);
    CHECK(fb_row(&f, 121 * 2)[141 * 2] == 0xFFFFFFFFu, "white square changed");
    fb_free(&f);
}

int main(void)
{
    flat_images_stay_flat();
    lone_pixel();
    staircase();
    presets();
    scale2_matches_scale1();
    glow();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("enhance: all checks passed");
    return 0;
}
