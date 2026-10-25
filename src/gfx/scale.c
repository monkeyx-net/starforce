#include "gfx/scale.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint32_t *px;
    int w, h, stride, clamp;
} Src;

static uint32_t at(const Src *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) {
        if (!s->clamp) return 0;
        x = x < 0 ? 0 : x >= s->w ? s->w - 1 : x;
        y = y < 0 ? 0 : y >= s->h ? s->h - 1 : y;
    }
    return s->px[(long)y * s->stride + x];
}

/* Straight ARGB to premultiplied and back; alpha 0 is always 0x00000000. */
static uint32_t premul(uint32_t c)
{
    uint32_t a = c >> 24;
    if (!a) return 0;
    if (a == 255) return c;
    uint32_t r = (c >> 16 & 0xFF) * a / 255, g = (c >> 8 & 0xFF) * a / 255, b = (c & 0xFF) * a / 255;
    return a << 24 | r << 16 | g << 8 | b;
}

static uint32_t unpremul(uint32_t c)
{
    uint32_t a = c >> 24;
    if (!a) return 0;
    if (a == 255) return c;
    uint32_t r = (c >> 16 & 0xFF) * 255 / a, g = (c >> 8 & 0xFF) * 255 / a, b = (c & 0xFF) * 255 / a;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return a << 24 | r << 16 | g << 8 | b;
}

/* ---- Scale2x / Scale3x ------------------------------------------------- */

static void scale2x(const Src *s, uint32_t *dst, int dstride)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t B, D, E, F, H;
            if (x > 0 && y > 0 && x < s->w - 1 && y < s->h - 1) { /* inside: no edge to clamp */
                const uint32_t *p = s->px + (long)y * s->stride + x;
                B = p[-s->stride], D = p[-1], E = p[0], F = p[1], H = p[s->stride];
            } else {
                B = at(s, x, y - 1), D = at(s, x - 1, y), E = at(s, x, y), F = at(s, x + 1, y), H = at(s, x, y + 1);
            }
            uint32_t *o = dst + (long)(y * 2) * dstride + x * 2;
            if (B != H && D != F) {
                o[0] = D == B ? D : E;
                o[1] = B == F ? F : E;
                o[dstride] = D == H ? D : E;
                o[dstride + 1] = H == F ? F : E;
            } else {
                o[0] = o[1] = o[dstride] = o[dstride + 1] = E;
            }
        }
}

static void scale3x(const Src *s, uint32_t *dst, int dstride)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t A = at(s, x - 1, y - 1), B = at(s, x, y - 1), C = at(s, x + 1, y - 1);
            uint32_t D = at(s, x - 1, y), E = at(s, x, y), F = at(s, x + 1, y);
            uint32_t G = at(s, x - 1, y + 1), H = at(s, x, y + 1), I = at(s, x + 1, y + 1);
            uint32_t *o0 = dst + (long)(y * 3) * dstride + x * 3, *o1 = o0 + dstride, *o2 = o1 + dstride;
            if (B != H && D != F) {
                o0[0] = D == B ? D : E;
                o0[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                o0[2] = B == F ? F : E;
                o1[0] = (D == B && E != G) || (D == H && E != A) ? D : E;
                o1[1] = E;
                o1[2] = (B == F && E != I) || (H == F && E != C) ? F : E;
                o2[0] = D == H ? D : E;
                o2[1] = (D == H && E != I) || (H == F && E != G) ? H : E;
                o2[2] = H == F ? F : E;
            } else {
                o0[0] = o0[1] = o0[2] = o1[0] = o1[1] = o1[2] = o2[0] = o2[1] = o2[2] = E;
            }
        }
}

/* ---- xBR (2x, 3x) ------------------------------------------------------- */

static int iabs(int v) { return v < 0 ? -v : v; }

/* Colour distance in weighted YUV (xBR's 48:7:6), plus alpha. */
static int df(uint32_t a, uint32_t b)
{
    int dr = (int)(a >> 16 & 0xFF) - (int)(b >> 16 & 0xFF), dg = (int)(a >> 8 & 0xFF) - (int)(b >> 8 & 0xFF);
    int db = (int)(a & 0xFF) - (int)(b & 0xFF), da = (int)(a >> 24) - (int)(b >> 24);
    int y = iabs(299 * dr + 587 * dg + 114 * db), u = iabs(-169 * dr - 331 * dg + 500 * db),
        v = iabs(500 * dr - 419 * dg - 81 * db);
    return (48 * y + 7 * u + 6 * v) / 1000 + 48 * iabs(da);
}

#define EQ_THRESHOLD 480 /* about 10 luma steps */
static int eq(uint32_t a, uint32_t b) { return df(a, b) < EQ_THRESHOLD; }

/* dst moved towards src by w/256, per channel (premultiplied) */
static uint32_t blend(uint32_t dst, uint32_t src, int w)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int d = (int)(dst >> sh & 0xFF), c = (int)(src >> sh & 0xFF);
        out |= (uint32_t)(d + ((c - d) * w + 128) / 256) << sh;
    }
    return out;
}

/* The four corners are one rule in four rotations: rotated "down" and
   "right" as (row, column) steps. Neighbours, rotated so the corner being
   filled is bottom-right:
          .  B1 .
       .  PA PB PC .
       D0 PD PE PF F4
       .  PG PH PI I4
          .  H5 I5                                                            */
static const int ROT[4][2][2] = {
    { { 1, 0 }, { 0, 1 } },   /* bottom-right */
    { { 0, 1 }, { -1, 0 } },  /* top-right */
    { { -1, 0 }, { 0, -1 } }, /* top-left */
    { { 0, -1 }, { 1, 0 } },  /* bottom-left */
};

typedef struct {
    uint32_t PE, PI, PH, PF, PG, PC, PD, PB, F4, I4, H5, I5;
} Hood;

static Hood hood(const Src *s, int x, int y, int rot)
{
    const int (*r)[2] = ROT[rot];
#define N(dr, dc) at(s, x + (dr) * r[0][1] + (dc) * r[1][1], y + (dr) * r[0][0] + (dc) * r[1][0])
    Hood h = { N(0, 0), N(1, 1), N(1, 0), N(0, 1), N(1, -1), N(-1, 1), N(0, -1), N(-1, 0),
               N(0, 2), N(1, 2), N(2, 0), N(2, 1) };
#undef N
    return h;
}

/* Output cell (row, col) of the f x f block, rotated: index into E. */
static int cell(int rot, int f, int row, int col)
{
    const int (*r)[2] = ROT[rot];
    int dr = 2 * row - (f - 1), dc = 2 * col - (f - 1); /* doubled offsets from the centre */
    int ar = dr * r[0][0] + dc * r[1][0], ac = dr * r[0][1] + dc * r[1][1];
    return (ar + f - 1) / 2 * f + (ac + f - 1) / 2;
}

/* Edge test shared by 2x and 3x: 0 none, 1 weak (blend the corner), 2 strong. */
static int edge(const Hood *h, int f, uint32_t *px, int *left, int *up)
{
    if (h->PE == h->PH || h->PE == h->PF) return 0;
    int e = df(h->PE, h->PC) + df(h->PE, h->PG) + df(h->PI, h->H5) + df(h->PI, h->F4) + (df(h->PH, h->PF) << 2);
    int i = df(h->PH, h->PD) + df(h->PH, h->I5) + df(h->PF, h->I4) + df(h->PF, h->PB) + (df(h->PE, h->PI) << 2);
    if (e > i) return 0;
    *px = df(h->PE, h->PF) <= df(h->PE, h->PH) ? h->PF : h->PH;
    int strong = f == 2
        ? (!eq(h->PF, h->PB) && !eq(h->PH, h->PD)) || (eq(h->PE, h->PI) && !eq(h->PF, h->I4) && !eq(h->PH, h->I5)) ||
              eq(h->PE, h->PG) || eq(h->PE, h->PC)
        : (!eq(h->PF, h->PB) && !eq(h->PF, h->PC)) || (!eq(h->PH, h->PD) && !eq(h->PH, h->PG)) ||
              (eq(h->PE, h->PI) && ((!eq(h->PF, h->F4) && !eq(h->PF, h->I4)) ||
                                    (!eq(h->PH, h->H5) && !eq(h->PH, h->I5)))) ||
              eq(h->PE, h->PG) || eq(h->PE, h->PC);
    if (e == i || !strong) return 1;
    int ke = df(h->PF, h->PG), ki = df(h->PH, h->PC);
    *left = ke << 1 <= ki && h->PE != h->PG && h->PD != h->PG;
    *up = ke >= ki << 1 && h->PE != h->PC && h->PB != h->PC;
    return 2;
}

/* Input premultiplied; f is 2 or 3. */
static void xbr(const Src *s, uint32_t *dst, int dstride, int f)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t E[9], pe, up, down, lt, rt;
            if (x > 0 && y > 0 && x < s->w - 1 && y < s->h - 1) { /* inside: no edge to clamp */
                const uint32_t *p = s->px + (long)y * s->stride + x;
                pe = p[0], up = p[-s->stride], down = p[s->stride], lt = p[-1], rt = p[1];
            } else {
                pe = at(s, x, y), up = at(s, x, y - 1), down = at(s, x, y + 1), lt = at(s, x - 1, y), rt = at(s, x + 1, y);
            }
            /* every corner needs PE to differ from both its edge neighbours:
               flat areas (most of the screen) skip the neighbourhood */
            int open = (pe != down && pe != rt) || (pe != rt && pe != up) || (pe != up && pe != lt) ||
                       (pe != lt && pe != down);
            if (!open) {
                for (int r = 0; r < f; r++)
                    for (int c = 0; c < f; c++) dst[(long)(y * f + r) * dstride + x * f + c] = pe;
                continue;
            }
            for (int i = 0; i < f * f; i++) E[i] = pe;
            for (int rot = 0; rot < 4; rot++) {
                Hood h = hood(s, x, y, rot);
                uint32_t px = 0;
                int left = 0, up = 0, kind = edge(&h, f, &px, &left, &up);
                if (!kind) continue;
                if (f == 2) {
                    int n1 = cell(rot, 2, 0, 1), n2 = cell(rot, 2, 1, 0), n3 = cell(rot, 2, 1, 1);
                    if (kind == 1) {
                        E[n3] = blend(E[n3], px, 64);
                    } else if (left || up) {
                        E[n3] = blend(E[n3], px, 192);
                        if (left) E[n2] = blend(E[n2], px, 64);
                        if (up) E[n1] = blend(E[n1], px, 64);
                    } else {
                        E[n3] = blend(E[n3], px, 128);
                    }
                } else {
                    int n2 = cell(rot, 3, 0, 2), n5 = cell(rot, 3, 1, 2), n6 = cell(rot, 3, 2, 0),
                        n7 = cell(rot, 3, 2, 1), n8 = cell(rot, 3, 2, 2);
                    if (kind == 1) {
                        E[n8] = blend(E[n8], px, 128);
                    } else if (left && up) {
                        E[n7] = blend(E[n7], px, 192);
                        E[n6] = blend(E[n6], px, 64);
                        E[n5] = E[n7];
                        E[n2] = E[n6];
                        E[n8] = px;
                    } else if (left) {
                        E[n7] = blend(E[n7], px, 192);
                        E[n5] = blend(E[n5], px, 64);
                        E[n6] = blend(E[n6], px, 64);
                        E[n8] = px;
                    } else if (up) {
                        E[n5] = blend(E[n5], px, 192);
                        E[n7] = blend(E[n7], px, 64);
                        E[n2] = blend(E[n2], px, 64);
                        E[n8] = px;
                    } else {
                        E[n8] = blend(E[n8], px, 224);
                        E[n5] = blend(E[n5], px, 32);
                        E[n7] = blend(E[n7], px, 32);
                    }
                }
            }
            for (int r = 0; r < f; r++)
                for (int c = 0; c < f; c++) dst[(long)(y * f + r) * dstride + x * f + c] = E[r * f + c];
        }
}

/* ---- entry point --------------------------------------------------------- */

/* One 2x or 3x pass of `scaler` from src to a packed w*f x h*f buffer. */
static void pass(const Src *s, uint32_t *dst, int f, Scaler scaler)
{
    if (scaler == SCALER_SMOOTH) xbr(s, dst, s->w * f, f);
    else if (f == 3) scale3x(s, dst, s->w * 3);
    else scale2x(s, dst, s->w * 2);
}

/* Work buffers, kept between calls: the whole-frame filter runs every frame. */
static uint32_t *in, *mid, *big;
static size_t capacity; /* pixels of `in` */

static int reserve(size_t pixels)
{
    if (pixels <= capacity) return 0;
    uint32_t *a = realloc(in, sizeof *a * pixels);
    if (a) in = a;
    uint32_t *b = realloc(mid, sizeof *b * pixels * 16);
    if (b) mid = b;
    uint32_t *c = realloc(big, sizeof *c * pixels * 16);
    if (c) big = c;
    if (!a || !b || !c) return -1;
    capacity = pixels;
    return 0;
}

int scale_pixels(const uint32_t *src, int w, int h, int sstride, uint32_t *dst, int dstride,
                 int factor, Scaler scaler, int clamp)
{
    if (w <= 0 || h <= 0) return 0;
    if (reserve((size_t)w * (size_t)h)) return -1;
    /* work on premultiplied pixels so blends fade sprite edges to transparent
       (an opaque picture clamped at its edges, the game's frame, is its own:
       blends keep it opaque) */
    uint32_t alpha = 0xFF000000u;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = src[(long)y * sstride + x];
            alpha &= c;
            in[(long)y * w + x] = premul(c);
        }
    int opaque = clamp && alpha == 0xFF000000u;

    Src s = { in, w, h, w, clamp };
    int ow = w * factor, oh = h * factor;
    const uint32_t *res;
    if (factor == 2 || factor == 3) {
        pass(&s, mid, factor, scaler);
        res = mid;
    } else { /* 4x: two 2x passes */
        pass(&s, mid, 2, scaler);
        Src s2 = { mid, w * 2, h * 2, w * 2, clamp };
        pass(&s2, big, 2, scaler);
        res = big;
    }
    for (int y = 0; y < oh; y++)
        if (opaque) memcpy(dst + (long)y * dstride, res + (long)y * ow, sizeof *dst * (size_t)ow);
        else
            for (int x = 0; x < ow; x++) dst[(long)y * dstride + x] = unpremul(res[(long)y * ow + x]);
    return 0;
}
