#include "platform/video.h"

#include <SDL.h>

#define SCANLINE_SHADE 150 /* brightness of a scanline's dark rows, /255 */
#define WINDOW_FILL 0.9   /* a window's share of the desktop, at most */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture, *scanlines_tex, *target, *glow_tex;
static int tex_w, tex_h, scanlines_scale, target_scale;
static bool accelerated;
static ScreenSettings screen;

void video_set_screen(const ScreenSettings *s)
{
    screen = *s;
}

/* The largest integer scale whose window fits the desktop (3 if unknown) */
static int window_scale(int w, int h)
{
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(0, &r)) return 3;
    int scale = 1;
    while ((scale + 1) * w <= r.w * WINDOW_FILL && (scale + 1) * h <= r.h * WINDOW_FILL) scale++;
    return scale;
}

bool video_init(bool windowed)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */

    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    int turned = screen.rotate == 90 || screen.rotate == 270;
    int w = turned ? FB_HEIGHT : FB_WIDTH, h = turned ? FB_WIDTH : FB_HEIGHT, scale = window_scale(w, h);
    window = SDL_CreateWindow("Star Force", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, h * scale, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) /* KMSDRM without GLES, or a dummy driver */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_RendererInfo info;
    accelerated = renderer && !SDL_GetRendererInfo(renderer, &info) && (info.flags & SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_ShowCursor(SDL_DISABLE);
    return true;
}

static void layout(ScreenLayout *l)
{
    int out_w, out_h;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    screen_layout(&screen, out_w, out_h, l);
}

int video_display_scale(void)
{
    ScreenLayout l;
    layout(&l);
    return l.crisp > FB_MAX_SCALE && !l.exact ? FB_MAX_SCALE : l.crisp;
}

/* A 1-px-high row, one column per output column: the monitor is turned, so
   its scanlines run down the screen; the last third of each arcade column
   is dark. */
static void build_scanlines(int scale)
{
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    scanlines_scale = scale;
    if (scale > 16) scale = 16; /* stretched beyond that */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    scanlines_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                      FB_WIDTH * scale, 1);
    if (!scanlines_tex) return;
    static Uint32 cols[FB_WIDTH * 16];
    int dark = scale / 3 > 0 ? scale / 3 : 1;
    for (int x = 0; x < FB_WIDTH * scale; x++) {
        Uint32 v = x % scale >= scale - dark ? SCANLINE_SHADE : 255;
        cols[x] = 0xFF000000u | v << 16 | v << 8 | v;
    }
    SDL_UpdateTexture(scanlines_tex, NULL, cols, FB_WIDTH * scale * (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(scanlines_tex, SDL_BLENDMODE_MOD);
}

/* Fitted pictures are drawn crisp at the next integer scale into `target`,
   which is then scaled down smoothly (sharp bilinear). */
static SDL_Texture *fit_target(int scale)
{
    if (target && target_scale == scale) return target;
    if (target) SDL_DestroyTexture(target);
    target = NULL;
    target_scale = scale;
    if (!SDL_RenderTargetSupported(renderer)) return NULL;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* linear, read when the texture is made */
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                               FB_WIDTH * scale, FB_HEIGHT * scale);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    return target;
}

bool video_gpu_glow(void) { return accelerated; }

/* The glow's grid as a texture, stretched linearly and added */
static SDL_Texture *glow_texture(const uint32_t *glow)
{
    if (!glow_tex) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* linear, read when the texture is made */
        glow_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, ENHANCE_GLOW_W,
                                     ENHANCE_GLOW_H);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        if (!glow_tex) return NULL;
        SDL_SetTextureBlendMode(glow_tex, SDL_BLENDMODE_ADD);
    }
    SDL_UpdateTexture(glow_tex, NULL, glow, ENHANCE_GLOW_W * (int)sizeof(uint32_t));
    return glow_tex;
}

void video_present(const Framebuffer *fb, bool scanlines, const uint32_t *glow)
{
    if (!texture || tex_w != fb->w || tex_h != fb->h) {
        if (texture) SDL_DestroyTexture(texture);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fb->w, fb->h);
        tex_w = fb->w;
        tex_h = fb->h;
        if (!texture) {
            SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
            return;
        }
    }
    SDL_UpdateTexture(texture, NULL, fb->px, fb->w * (int)sizeof(uint32_t));

    ScreenLayout l;
    layout(&l);
    SDL_Rect dst = { l.x, l.y, l.w, l.h };
    const double angle = screen.rotate; /* clockwise, about the picture's centre: the screen's */

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    int crisp = l.crisp; /* device pixels per arcade pixel before fitting */
    SDL_Texture *g = glow ? glow_texture(glow) : NULL;
    SDL_Texture *t = l.exact ? NULL : fit_target(crisp);
    if (t && !SDL_SetRenderTarget(renderer, t)) {
        SDL_Rect whole = { 0, 0, FB_WIDTH * crisp, FB_HEIGHT * crisp };
        SDL_RenderCopy(renderer, texture, NULL, &whole);
        if (g) SDL_RenderCopy(renderer, g, NULL, &whole);
        if (scanlines && crisp >= 2) {
            if (scanlines_scale != crisp) build_scanlines(crisp);
            if (scanlines_tex) SDL_RenderCopy(renderer, scanlines_tex, NULL, &whole);
        }
        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderCopyEx(renderer, t, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
    } else {
        SDL_RenderCopyEx(renderer, texture, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
        if (g) SDL_RenderCopyEx(renderer, g, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
        if (scanlines && crisp >= 2) { /* one output column per arcade column has no room for a scanline */
            if (scanlines_scale != crisp) build_scanlines(crisp);
            if (scanlines_tex) SDL_RenderCopyEx(renderer, scanlines_tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
        }
    }
    SDL_RenderPresent(renderer);
}

void video_shutdown(void)
{
    if (target) SDL_DestroyTexture(target);
    if (glow_tex) SDL_DestroyTexture(glow_tex);
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    target = scanlines_tex = texture = glow_tex = NULL;
    renderer = NULL;
    window = NULL;
    tex_w = tex_h = scanlines_scale = target_scale = 0;
}
