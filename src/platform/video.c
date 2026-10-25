#include "platform/video.h"

#include <SDL.h>

#define SCANLINE_SHADE 150 /* brightness of a scanline's dark rows, /255 */
/* An integer scale is used when it fills at least this much of the fitted
   size; otherwise the picture is scaled to fit (640x480: 1.667x, pillarboxed). */
#define INTEGER_FILL 0.9

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture, *scanlines_tex, *target;
static int tex_w, tex_h, scanlines_scale, target_scale;

bool video_init(bool windowed)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */

    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    window = SDL_CreateWindow("Star Force", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              FB_WIDTH * 3, FB_HEIGHT * 3, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) /* KMSDRM without GLES, or a dummy driver */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_ShowCursor(SDL_DISABLE);
    return true;
}

/* The largest scale that fits the window, and whether it is used as is (an
   integer scale) or the picture is fitted. */
static double fit_scale(int *integer)
{
    int out_w, out_h;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    double fx = (double)out_w / FB_WIDTH, fy = (double)out_h / FB_HEIGHT;
    double fit = fx < fy ? fx : fy;
    int whole = (int)fit;
    *integer = fit < 1 || whole >= fit * INTEGER_FILL;
    return *integer ? (whole < 1 ? 1 : whole) : fit;
}

int video_display_scale(void)
{
    int integer;
    double fit = fit_scale(&integer);
    int scale = integer ? (int)fit : (int)fit + 1;
    return scale > FB_MAX_SCALE && !integer ? FB_MAX_SCALE : scale;
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

void video_present(const Framebuffer *fb, bool scanlines)
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

    int out_w, out_h, integer;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    double fit = fit_scale(&integer);
    int w = (int)(FB_WIDTH * fit), h = (int)(FB_HEIGHT * fit);
    SDL_Rect dst = { (out_w - w) / 2, (out_h - h) / 2, w, h };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    int crisp = integer ? (int)fit : (int)fit + 1; /* device pixels per arcade pixel before fitting */
    SDL_Texture *t = integer ? NULL : fit_target(crisp);
    if (t && !SDL_SetRenderTarget(renderer, t)) {
        SDL_Rect whole = { 0, 0, FB_WIDTH * crisp, FB_HEIGHT * crisp };
        SDL_RenderCopy(renderer, texture, NULL, &whole);
        if (scanlines && crisp >= 2) {
            if (scanlines_scale != crisp) build_scanlines(crisp);
            if (scanlines_tex) SDL_RenderCopy(renderer, scanlines_tex, NULL, &whole);
        }
        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderCopy(renderer, t, NULL, &dst);
    } else {
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        if (scanlines && crisp >= 2) { /* one output column per arcade column has no room for a scanline */
            if (scanlines_scale != crisp) build_scanlines(crisp);
            if (scanlines_tex) SDL_RenderCopy(renderer, scanlines_tex, NULL, &dst);
        }
    }
    SDL_RenderPresent(renderer);
}

void video_shutdown(void)
{
    if (target) SDL_DestroyTexture(target);
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    target = scanlines_tex = texture = NULL;
    renderer = NULL;
    window = NULL;
    tex_w = tex_h = scanlines_scale = target_scale = 0;
}
