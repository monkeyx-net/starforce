#include "gfx/blit.h"

#include <math.h>

#define MAX_HIRES 64

static struct {
    const Image *lo, *hi;
    int factor;
} hires[MAX_HIRES];
static int hires_count;

void blit_set_hires(const Image *lo, const Image *hi, int factor)
{
    for (int i = 0; i < hires_count; i++)
        if (hires[i].lo == lo) {
            if (hi) {
                hires[i].hi = hi;
                hires[i].factor = factor;
            } else {
                hires[i] = hires[--hires_count];
            }
            return;
        }
    if (hi && hires_count < MAX_HIRES) {
        hires[hires_count].lo = lo;
        hires[hires_count].hi = hi;
        hires[hires_count].factor = factor;
        hires_count++;
    }
}

static const Image *hires_for(const Image *lo, int factor)
{
    if (factor < 2) return NULL;
    for (int i = 0; i < hires_count; i++)
        if (hires[i].lo == lo && hires[i].factor == factor) return hires[i].hi;
    return NULL;
}

/* Pixels whose centres lie in [a, b): first..last inclusive (last < first if none). */
static void span(double a, double b, int limit, int *first, int *last)
{
    int f = (int)ceil(a - 0.5);
    int l = (int)ceil(b - 0.5) - 1;
    if (f < 0) f = 0;
    if (l > limit - 1) l = limit - 1;
    *first = f;
    *last = l;
}

/* round(a * b / 255) for 8-bit channels, as the GPU writes a normalized product. */
static uint32_t mul8(uint32_t a, uint32_t b)
{
    return (a * b * 2 + 255) / 510;
}

void blit(Framebuffer *fb, const Image *img, int sx, int sy, int sw, int sh,
          double dx, double dy, int scale, uint32_t tint)
{
    blit_ex(fb, img, sx, sy, sw, sh, dx, dy, scale, tint, 1.0, 0);
}

void blit_ex(Framebuffer *fb, const Image *img, int sx, int sy, int sw, int sh,
             double dx, double dy, int scale, uint32_t tint, double alpha, int silhouette)
{
    uint32_t sprite_a = alpha >= 1 ? 255 : alpha <= 0 ? 0 : (uint32_t)(alpha * 255 + 0.5);
    if (!sprite_a) return;
    if (scale < 1) scale = 1;
    const int s = fb->scale;
    const Image *src = hires_for(img, s);
    const int f = src ? s : 1; /* source texels per image pixel */
    if (!src) src = img;
    int x0, x1, y0, y1;
    span(dx * s, (dx + (double)sw * scale) * s, fb->w, &x0, &x1);
    span(dy * s, (dy + (double)sh * scale) * s, fb->h, &y0, &y1);
    uint32_t tr = tint >> 16 & 0xFF, tg = tint >> 8 & 0xFF, tb = tint & 0xFF;

    for (int y = y0; y <= y1; y++) {
        int v = (int)floor(((y + 0.5) / s - dy) / scale * f);
        if (v < 0 || v >= sh * f) continue;
        const uint8_t *row = src->rgba + ((size_t)(sy * f + v) * (size_t)src->width + (size_t)(sx * f)) * 4;
        uint32_t *out = fb_row(fb, y);
        for (int x = x0; x <= x1; x++) {
            int u = (int)floor(((x + 0.5) / s - dx) / scale * f);
            if (u < 0 || u >= sw * f) continue;
            const uint8_t *p = row + (size_t)u * 4;
            uint32_t a = p[3];
            if (!a) continue;
            if (silhouette) a = 255;
            a = mul8(a, sprite_a);
            uint32_t r = silhouette ? tr : mul8(p[0], tr), g = silhouette ? tg : mul8(p[1], tg),
                     b = silhouette ? tb : mul8(p[2], tb);
            uint32_t *d = &out[x];
            if (a < 255) { /* source-over */
                uint32_t dr = *d >> 16 & 0xFF, dg = *d >> 8 & 0xFF, db = *d & 0xFF;
                r = mul8(r, a) + mul8(dr, 255 - a);
                g = mul8(g, a) + mul8(dg, 255 - a);
                b = mul8(b, a) + mul8(db, 255 - a);
            }
            *d = 0xFF000000u | r << 16 | g << 8 | b;
        }
    }
}

void fill_rectf(Framebuffer *fb, double x, double y, double w, double h, uint32_t rgb)
{
    const int s = fb->scale;
    int x0, x1, y0, y1;
    span(x * s, (x + w) * s, fb->w, &x0, &x1);
    span(y * s, (y + h) * s, fb->h, &y0, &y1);
    for (int yy = y0; yy <= y1; yy++) {
        uint32_t *row = fb_row(fb, yy);
        for (int xx = x0; xx <= x1; xx++) row[xx] = 0xFF000000u | (rgb & 0xFFFFFF);
    }
}

void blend_device(Framebuffer *fb, int x, int y, uint32_t rgb, double alpha)
{
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    uint32_t a = alpha >= 1 ? 255 : alpha <= 0 ? 0 : (uint32_t)(alpha * 255 + 0.5);
    uint32_t *d = &fb_row(fb, y)[x];
    uint32_t r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
    uint32_t dr = *d >> 16 & 0xFF, dg = *d >> 8 & 0xFF, db = *d & 0xFF;
    r = mul8(r, a) + mul8(dr, 255 - a);
    g = mul8(g, a) + mul8(dg, 255 - a);
    b = mul8(b, a) + mul8(db, 255 - a);
    *d = 0xFF000000u | r << 16 | g << 8 | b;
}

void blend_pixel(Framebuffer *fb, int x, int y, uint32_t rgb, double alpha)
{
    const int s = fb->scale;
    for (int yy = 0; yy < s; yy++)
        for (int xx = 0; xx < s; xx++) blend_device(fb, x * s + xx, y * s + yy, rgb, alpha);
}

void fill_rectf_alpha(Framebuffer *fb, double x, double y, double w, double h, uint32_t rgb, double alpha)
{
    const int s = fb->scale;
    int x0, x1, y0, y1;
    span(x * s, (x + w) * s, fb->w, &x0, &x1);
    span(y * s, (y + h) * s, fb->h, &y0, &y1);
    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++) blend_device(fb, xx, yy, rgb, alpha);
}
