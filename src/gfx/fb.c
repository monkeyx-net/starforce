#include "gfx/fb.h"

#include <stdlib.h>

int fb_init(Framebuffer *fb, int scale)
{
    if (scale < 1) scale = 1;
    if (scale > FB_MAX_SCALE) scale = FB_MAX_SCALE;
    if (fb->px && fb->scale == scale) return 0;
    uint32_t *px = realloc(fb->px, sizeof *px * FB_WIDTH * scale * FB_HEIGHT * scale);
    if (!px) return -1;
    fb->px = px;
    fb->scale = scale;
    fb->w = FB_WIDTH * scale;
    fb->h = FB_HEIGHT * scale;
    fb_clear(fb, 0xFF000000u);
    return 0;
}

void fb_free(Framebuffer *fb)
{
    free(fb->px);
    fb->px = NULL;
    fb->scale = fb->w = fb->h = 0;
}

void fb_clear(Framebuffer *fb, uint32_t argb)
{
    for (long i = 0, n = (long)fb->w * fb->h; i < n; i++) fb->px[i] = argb;
}

void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t argb)
{
    const int s = fb->scale;
    int x0 = x < 0 ? 0 : x * s;
    int y0 = y < 0 ? 0 : y * s;
    int x1 = (x + w) * s > fb->w ? fb->w : (x + w) * s;
    int y1 = (y + h) * s > fb->h ? fb->h : (y + h) * s;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = fb_row(fb, yy);
        for (int xx = x0; xx < x1; xx++) row[xx] = argb;
    }
}

void fb_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    fb_fill_rect(fb, x, y, w, 1, argb);
    fb_fill_rect(fb, x, y + h - 1, w, 1, argb);
    fb_fill_rect(fb, x, y, 1, h, argb);
    fb_fill_rect(fb, x + w - 1, y, 1, h, argb);
}
