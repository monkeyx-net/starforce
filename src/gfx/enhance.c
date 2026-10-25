#include "gfx/enhance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/blit.h"

/* ---- settings and presets ------------------------------------------------ */

static const struct {
    const char *name;
    GfxSettings g;
} presets[] = {
    { "classic", { GFX_CLASSIC, 0, 0, 0 } },
    { "classic-crt", { GFX_CLASSIC, 0, 0, 1 } },
    { "crisp", { GFX_FILTER, 0, 0, 0 } },
    { "smooth", { GFX_FILTER, 1, 0, 0 } },
    { "hd-crisp", { GFX_HD, 0, 0, 0 } },
    { "hd-smooth", { GFX_HD, 1, 0, 0 } },
    { "hd-glow", { GFX_HD, 1, 1, 0 } },
    { "hd-glow-crt", { GFX_HD, 1, 1, 1 } },
};
#define PRESETS (int)(sizeof presets / sizeof presets[0])

static int same(const GfxSettings *a, const GfxSettings *b)
{
    return a->mode == b->mode && (a->mode == GFX_CLASSIC || a->smooth == b->smooth) && a->glow == b->glow &&
           a->scanlines == b->scanlines;
}

const char *gfx_describe(const GfxSettings *g)
{
    static char text[64];
    snprintf(text, sizeof text, "%s%s%s%s", g->mode == GFX_HD ? "HD " : "",
             g->mode == GFX_CLASSIC ? "CLASSIC" : g->smooth ? "SMOOTH" : "CRISP", g->glow ? " + GLOW" : "",
             g->scanlines ? " + CRT" : "");
    return text;
}

void gfx_next_preset(GfxSettings *g)
{
    int i = 0;
    while (i < PRESETS && !same(&presets[i].g, g)) i++;
    *g = presets[i < PRESETS ? (i + 1) % PRESETS : 0].g;
}

int gfx_parse(const char *name, GfxSettings *g)
{
    for (int i = 0; i < PRESETS; i++)
        if (!strcmp(presets[i].name, name)) {
            *g = presets[i].g;
            return 0;
        }
    return -1;
}

const char *gfx_preset_names(void)
{
    static char text[160];
    if (!text[0])
        for (int i = 0; i < PRESETS; i++) {
            if (i) strncat(text, " ", sizeof text - strlen(text) - 1);
            strncat(text, presets[i].name, sizeof text - strlen(text) - 1);
        }
    return text;
}

int gfx_render_scale(const GfxSettings *g, int display_scale)
{
    if (g->mode == GFX_CLASSIC) return 1;
    int s = display_scale < 2 ? 2 : display_scale;
    return s > FB_MAX_SCALE ? FB_MAX_SCALE : s;
}

/* ---- HD assets ------------------------------------------------------------ */

#define MAX_IMAGES 32

static struct {
    const Image *lo;
    EnhanceRect *rects;
    int count, crisp;
    Image hi;
} images[MAX_IMAGES];
static int image_count;
static int prepared_factor;
static Scaler prepared_scaler;

/* Upscale each rect of lo on its own (transparent around it) into hi, factor f. */
static int upscale_rects(const Image *lo, const EnhanceRect *rects, int n, int f, Scaler sc, Image *hi)
{
    hi->width = lo->width * f;
    hi->height = lo->height * f;
    hi->rgba = calloc((size_t)hi->width * (size_t)hi->height, 4);
    if (!hi->rgba) return -1;
    for (int i = 0; i < n; i++) {
        const EnhanceRect *r = &rects[i];
        if (r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0 || r->x + r->w > lo->width || r->y + r->h > lo->height)
            continue;
        uint32_t *in = malloc(sizeof *in * (size_t)r->w * (size_t)r->h);
        uint32_t *out = malloc(sizeof *out * (size_t)r->w * (size_t)r->h * (size_t)(f * f));
        if (!in || !out) {
            free(in);
            free(out);
            return -1;
        }
        for (int y = 0; y < r->h; y++)
            for (int x = 0; x < r->w; x++) {
                const uint8_t *p = lo->rgba + ((size_t)(r->y + y) * (size_t)lo->width + (size_t)(r->x + x)) * 4;
                in[y * r->w + x] = p[3] ? (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2] : 0;
            }
        int rc = scale_pixels(in, r->w, r->h, r->w, out, r->w * f, f, sc, 0);
        for (int y = 0; !rc && y < r->h * f; y++)
            for (int x = 0; x < r->w * f; x++) {
                uint32_t c = out[y * r->w * f + x];
                uint8_t *o = hi->rgba + ((size_t)(r->y * f + y) * (size_t)hi->width + (size_t)(r->x * f + x)) * 4;
                o[0] = (uint8_t)(c >> 16);
                o[1] = (uint8_t)(c >> 8);
                o[2] = (uint8_t)c;
                o[3] = (uint8_t)(c >> 24);
            }
        free(in);
        free(out);
        if (rc) return -1;
    }
    return 0;
}

static void release(void)
{
    for (int i = 0; i < image_count; i++) {
        blit_set_hires(images[i].lo, NULL, 0);
        image_free(&images[i].hi);
    }
    prepared_factor = 0;
}

int enhance_add_image(const Image *img, const EnhanceRect *rects, int count, int crisp)
{
    if (image_count >= MAX_IMAGES || count <= 0) return -1;
    EnhanceRect *copy = malloc(sizeof *copy * (size_t)count);
    if (!copy) return -1;
    memcpy(copy, rects, sizeof *copy * (size_t)count);
    release(); /* rebuilt with the new image on the next prepare */
    images[image_count].lo = img;
    images[image_count].rects = copy;
    images[image_count].count = count;
    images[image_count].crisp = crisp;
    image_count++;
    return 0;
}

void enhance_clear_images(void)
{
    release();
    for (int i = 0; i < image_count; i++) free(images[i].rects);
    image_count = 0;
}

int enhance_prepare(int factor, Scaler scaler)
{
    if (factor < 2) return 0;
    if (prepared_factor == factor && prepared_scaler == scaler) return 0;
    release();
    for (int i = 0; i < image_count; i++) {
        Scaler sc = images[i].crisp ? SCALER_CRISP : scaler;
        if (upscale_rects(images[i].lo, images[i].rects, images[i].count, factor, sc, &images[i].hi)) {
            release();
            return -1;
        }
        blit_set_hires(images[i].lo, &images[i].hi, factor);
    }
    prepared_factor = factor;
    prepared_scaler = scaler;
    return 0;
}

/* ---- whole-frame filter --------------------------------------------------- */

int enhance_filter_frame(const Framebuffer *src, Framebuffer *dst, Scaler scaler)
{
    return scale_pixels(src->px, FB_WIDTH, FB_HEIGHT, src->w, dst->px, dst->w, dst->scale, scaler, 1);
}

/* ---- glow ----------------------------------------------------------------- */

#define GLOW_STRENGTH 384 /* /256 of the blurred frame added back */
#define GLOW_CELL 2       /* the blur runs on cells of 2 x 2 arcade pixels */
#define GLOW_RADIUS 1     /* box blur radius in cells, run twice */
#define GW (FB_WIDTH / GLOW_CELL)
#define GH (FB_HEIGHT / GLOW_CELL)

static int glow_buf[3][GH][GW], glow_tmp[3][GH][GW];
static uint32_t glow_px[GH][GW]; /* blurred, scaled by GLOW_STRENGTH, packed RGB */

#define BLUR_LEN (2 * GLOW_RADIUS + 1)
/* sum / BLUR_LEN for the small sums here (BLUR_LEN x 255 at most) */
#define BLUR_DIV(sum) ((sum) * (65536 / BLUR_LEN + 1) >> 16)

static int clampi(int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; }
static int glow_level(int v) { return clampi(v * GLOW_STRENGTH / 256, 255); }

/* Box blur of buf in place (tmp as scratch), rows then columns, by running sums;
   the column pass walks rows in order to stay in cache. */
static void box_blur(void)
{
    const int r = GLOW_RADIUS;
    static int col[GW];
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < GH; y++) {
            const int *in = glow_buf[c][y];
            int *out = glow_tmp[c][y];
            int sum = 0;
            for (int k = -r; k <= r; k++) sum += in[clampi(k, GW - 1)];
            for (int x = 0; x < GW; x++) {
                out[x] = BLUR_DIV(sum);
                sum += in[clampi(x + r + 1, GW - 1)] - in[clampi(x - r, GW - 1)];
            }
        }
        for (int x = 0; x < GW; x++) col[x] = 0;
        for (int k = -r; k <= r; k++)
            for (int x = 0; x < GW; x++) col[x] += glow_tmp[c][clampi(k, GH - 1)][x];
        for (int y = 0; y < GH; y++) {
            const int *add = glow_tmp[c][clampi(y + r + 1, GH - 1)], *sub = glow_tmp[c][clampi(y - r, GH - 1)];
            int *out = glow_buf[c][y];
            for (int x = 0; x < GW; x++) {
                out[x] = BLUR_DIV(col[x]);
                col[x] += add[x] - sub[x];
            }
        }
    }
}

/* Packed RGB a..b at w/256, all three channels at once. */
static uint32_t lerp_rgb(uint32_t a, uint32_t b, uint32_t w)
{
    uint32_t rb = ((a & 0xFF00FFu) * (256 - w) + (b & 0xFF00FFu) * w) >> 8 & 0xFF00FFu;
    uint32_t g = ((a & 0x00FF00u) * (256 - w) + (b & 0x00FF00u) * w) >> 8 & 0x00FF00u;
    return rb | g;
}

/* Opaque ARGB p plus packed RGB g, each channel saturating at 255. */
static uint32_t add_rgb(uint32_t p, uint32_t g)
{
    uint32_t rb = (p & 0xFF00FFu) + (g & 0xFF00FFu), gg = (p & 0x00FF00u) + (g & 0x00FF00u);
    rb |= (rb & 0x1000100u) - ((rb & 0x1000100u) >> 8);
    gg |= (gg & 0x10000u) - ((gg & 0x10000u) >> 8);
    return 0xFF000000u | (rb & 0xFF00FFu) | (gg & 0x00FF00u);
}

/* Bilinear taps from a grid of `cell` device pixels per entry, for device
   coordinate d: index i0 and weight of i0 + 1 (/256). */
static void taps(int d, int cell, int limit, int *i0, int *w1)
{
    int pos = ((2 * d + 1) * 256) / (2 * cell) - 128; /* (d + 0.5) / cell - 0.5, in 1/256 */
    int i = pos >> 8;
    *w1 = pos & 0xFF;
    if (i < 0) {
        i = 0;
        *w1 = 0;
    }
    if (i >= limit - 1) {
        i = limit - 1;
        *w1 = 0;
    }
    *i0 = i;
}

/* Grid row `row` of glow_px interpolated across the device width. */
static void glow_row(int row, uint32_t *out, int w, const int *xi, const int *xw)
{
    const uint32_t *g = glow_px[row];
    for (int x = 0; x < w; x++) {
        int x0 = xi[x], x1 = xw[x] ? x0 + 1 : x0;
        out[x] = (g[x0] | g[x1]) ? lerp_rgb(g[x0], g[x1], (uint32_t)xw[x]) : 0;
    }
}

void enhance_glow(Framebuffer *fb)
{
    const int cell = fb->scale * GLOW_CELL, n = cell * cell; /* device pixels per grid cell */
    /* average each cell, all three channels per add (n x 255 fits 16 bits) */
    static uint32_t acc_rb[GW], acc_g[GW];
    for (int gy = 0; gy < GH; gy++) {
        for (int gx = 0; gx < GW; gx++) acc_rb[gx] = acc_g[gx] = 0;
        for (int y = gy * cell; y < (gy + 1) * cell; y++) {
            const uint32_t *row = fb_row(fb, y);
            for (int gx = 0; gx < GW; gx++) {
                uint32_t rb = 0, g = 0;
                for (int k = 0; k < cell; k++) {
                    uint32_t p = row[gx * cell + k];
                    rb += p & 0xFF00FFu;
                    g += p & 0x00FF00u;
                }
                acc_rb[gx] += rb;
                acc_g[gx] += g;
            }
        }
        for (int gx = 0; gx < GW; gx++) {
            glow_buf[0][gy][gx] = (int)(acc_rb[gx] >> 16) / n;
            glow_buf[1][gy][gx] = (int)(acc_g[gx] >> 8) / n;
            glow_buf[2][gy][gx] = (int)(acc_rb[gx] & 0xFFFF) / n;
        }
    }
    box_blur();
    box_blur();
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            glow_px[y][x] = (uint32_t)glow_level(glow_buf[0][y][x]) << 16 |
                            (uint32_t)glow_level(glow_buf[1][y][x]) << 8 | (uint32_t)glow_level(glow_buf[2][y][x]);

    static int xi[FB_WIDTH * FB_MAX_SCALE], xw[FB_WIDTH * FB_MAX_SCALE];
    static uint32_t rows[2][FB_WIDTH * FB_MAX_SCALE];
    int cached[2] = { -1, -1 };
    for (int x = 0; x < fb->w; x++) taps(x, cell, GW, &xi[x], &xw[x]);
    for (int y = 0; y < fb->h; y++) {
        int yi, yw;
        taps(y, cell, GH, &yi, &yw);
        int y1 = yw ? yi + 1 : yi;
        const uint32_t *r0 = NULL, *r1 = NULL;
        for (int k = 0; k < 2; k++) { /* reuse interpolated rows across device rows */
            int want = k ? y1 : yi;
            int slot = cached[0] == want ? 0 : cached[1] == want ? 1 : -1;
            if (slot < 0) {
                slot = (cached[0] == yi || cached[0] == y1) ? 1 : 0;
                glow_row(want, rows[slot], fb->w, xi, xw);
                cached[slot] = want;
            }
            if (k) r1 = rows[slot];
            else r0 = rows[slot];
        }
        uint32_t *row = fb_row(fb, y);
        for (int x = 0; x < fb->w; x++) {
            uint32_t a = r0[x], b = r1[x];
            if (a | b) row[x] = add_rgb(row[x], lerp_rgb(a, b, (uint32_t)yw));
        }
    }
}
