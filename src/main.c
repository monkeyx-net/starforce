/*
 * Star Force native build: SDL setup and the fixed-rate loop (the board's
 * 59.19 Hz). See PROGRESS_STARFORCE_PORT.md.
 * With the player's ROM set the ROM's own code runs on the emulated board
 * (src/emu/), a frame per tick; the game in C follows (plan section 3).
 * Without it, or with --test-card, the test card (ROM status, an input
 * tester and the graphics presets).
 *
 *   starforce [--windowed] [--gfx preset] [--rom path] [--test-card] [--emulate]
 *          [--frames N] [--screenshot out.png]
 *   starforce --check-rom <dir|zip>
 *   starforce --dump-rom <dir|zip> [region]
 *   starforce --extract <dir|zip> [<asset dir>]
 *
 * --frames N stops after N updates; with --screenshot the last frame is
 * written as a PNG (use SDL_VIDEODRIVER=dummy to run headless).
 * --dump-rom prints "REGION:ADDR BYTE" lines (hex) for every byte of each
 * region (MAME's names: maincpu sub fgtiles bgtiles1 bgtiles2 bgtiles3
 * sprites dac), or of the one named: the format the MAME
 * probe writes (src/tests/mame/rom_map_probe.lua).
 * --extract writes the graphics, font, waveform and the programs' tables
 * from the ROM set to an asset folder (default: assets/ next to the
 * program, or $STARFORCE_ASSETS; assets/extract.h), which the game in C
 * reads instead of it; the first run with a ROM set does it too.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/assets.h"
#include "assets/extract.h"
#include "assets/png.h"
#include "assets/rom.h"
#include "assets/savefile.h"
#include "emu/board.h"
#include "gfx/blit.h"
#include "gfx/debugfont.h"
#include "gfx/enhance.h"
#include "gfx/fb.h"
#include "gfx/starforce.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/video.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* MAME: 6 MHz pixel clock, 384 x 264 */
#define FRAME_HZ (6000000.0 / (384 * 264))
/* Cap catch-up after a stall (suspend/resume, SD card hiccup). */
#define MAX_STEPS_PER_PRESENT 4
#define GFX_NOTICE_FRAMES 120
#define COIN_PULSE_FRAMES 2 /* a coin's switch closes this long (MAME's) */

#define WHITE 0xFFFFFFu
#define GREY 0x606060u
#define YELLOW 0xFFFF44u
#define CYAN 0x44FFFFu
#define GREEN 0x44FF44u
#define RED 0xFF4444u
#define ORANGE 0xFF8800u

static Framebuffer fb;     /* arcade resolution */
static Framebuffer fb_big; /* enhanced modes: the frame at the render scale */
static Framebuffer *shown; /* the frame last drawn */

typedef struct {
    unsigned long clock;
    InputFrame in;
    GfxSettings gfx;
    int gfx_notice;
    int coins, coins2;
    char rom_path[1024];
    char rom_error[256];
    char assets_path[1024]; /* the extracted assets, or "" */
    RomSet rom;
    enum { SHOW_CARD, SHOW_BOARD } show; /* the test card, the emulated board */
    Board board;
    SfGfx art;              /* the ROM set's graphics, decoded */
    int coin_pulse[2];      /* frames left of each coin switch's pulse */
    bool coin_held[2];
} Port;

static Port g;

/* ---- test card ------------------------------------------------------------ */

static void centred(Framebuffer *t, int y, const char *text, uint32_t rgb)
{
    debug_text(t, (FB_WIDTH - debug_text_width(text)) / 2.0, y, text, rgb);
}

static void lamp(Framebuffer *t, int x, int y, const char *label, int on)
{
    fill_rectf(t, x, y, 5, 5, on ? GREEN : GREY);
    debug_text(t, x + 8, y, label, on ? WHITE : GREY);
}

static void draw_scene(Framebuffer *t)
{
    char line[160];
    fb_clear(t, 0xFF000000u);
    fill_rectf(t, 4, 15, FB_WIDTH - 8, 2, ORANGE);
    fill_rectf(t, 4, FB_HEIGHT - 11, FB_WIDTH - 8, 2, ORANGE);
    fill_rectf(t, 4, 15, 2, FB_HEIGHT - 24, ORANGE);
    fill_rectf(t, FB_WIDTH - 6, 15, 2, FB_HEIGHT - 24, ORANGE);
    centred(t, 4, "STAR FORCE - NATIVE PORT - TEST CARD", YELLOW);

    if (g.rom.loaded) {
        snprintf(line, sizeof line, "ROM: %.46s", g.rom_path);
        centred(t, 26, line, GREEN);
        centred(t, 34, "MAME STARFORC SET OK", GREEN);
        snprintf(line, sizeof line, "MAIN $%02X $%02X $%02X  SUB $%02X $%02X $%02X", g.rom.main[0], g.rom.main[1],
                 g.rom.main[2], g.rom.sub[0], g.rom.sub[1], g.rom.sub[2]);
        centred(t, 42, line, GREEN);
    } else {
        centred(t, 26, g.rom_error[0] ? "ROM SET INCOMPLETE OR WRONG" : "NO ROM SET FOUND", RED);
        centred(t, 34, "PUT STARFORC.ZIP NEXT TO THE PROGRAM,", RED);
        centred(t, 42, "OR USE --ROM", RED);
    }
    if (g.assets_path[0]) {
        snprintf(line, sizeof line, "ASSETS: %.44s", g.assets_path);
        centred(t, 50, line, GREEN);
    } else {
        centred(t, 50, "NO ASSETS EXTRACTED YET", YELLOW);
    }

    const InputFrame *in = &g.in;
    snprintf(line, sizeof line, "CONTROLS%s", in->pads ? "  (PAD)" : "");
    debug_text(t, 16, 66, line, CYAN);
    for (int p = 0; p < 2; p++) {
        const PlayerInput *pi = &in->p[p];
        int x = 16 + p * 104;
        debug_text(t, x, 76, p ? "PLAYER 2" : "PLAYER 1", WHITE);
        lamp(t, x + 30, 86, "UP", pi->move_y < 0);
        lamp(t, x, 96, "LEFT", pi->move_x < 0);
        lamp(t, x + 56, 96, "RIGHT", pi->move_x > 0);
        lamp(t, x + 30, 106, "DOWN", pi->move_y > 0);
        lamp(t, x, 118, "FIRE", pi->fire);
    }
    lamp(t, 16, 130, "COIN A", in->coin);
    lamp(t, 72, 130, "COIN B", in->coin2);
    lamp(t, 128, 130, "1P", in->start);
    lamp(t, 164, 130, "2P", in->start2);
    snprintf(line, sizeof line, "COINS A %d  B %d", g.coins, g.coins2);
    debug_text(t, 16, 140, line, WHITE);

    /* something moving, to judge pacing and the enhanced modes */
    double phase = (double)(g.clock % 240) / 240.0;
    double bx = 16 + 180 * (phase < 0.5 ? phase * 2 : 2 - phase * 2);
    fill_rectf(t, 16, 166, 192, 1, GREY);
    fill_rectf(t, bx, 154, 12, 12, YELLOW);
    fill_rectf(t, bx + 3, 157, 6, 6, RED);
    for (int i = 0; i < 8; i++) {
        static const uint32_t bars[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
        fill_rectf(t, 16 + i * 24, 172, 24, 16, bars[i]);
    }

    snprintf(line, sizeof line, "GRAPHICS: %s", gfx_describe(&g.gfx));
    centred(t, 196, line, g.gfx_notice > 0 ? YELLOW : WHITE);
    centred(t, 204, "SELECT+L1 / F3, F4 GLOW, F5 SCANLINES", GREY);
    snprintf(line, sizeof line, "FRAME %lu   RENDER %dX", g.clock, t->scale);
    centred(t, 214, line, GREY);
    centred(t, 224, "EXIT: HOLD SELECT+START OR ESC", GREY);
    centred(t, 236, "CONVERTED BY MONKEYX", GREY);
}

/* ---- the game ---------------------------------------------------------------- */

/* The port's controls as the cabinet's switches (active high): each coin a
   pulse when pressed */
static void panel(const InputFrame *in, uint8_t *p1, uint8_t *p2, uint8_t *sys)
{
    uint8_t *p[2] = { p1, p2 };
    for (int i = 0; i < 2; i++) {
        const PlayerInput *pi = &in->p[i];
        *p[i] = (uint8_t)((pi->move_x > 0 ? IN_RIGHT : 0) | (pi->move_x < 0 ? IN_LEFT : 0) |
                          (pi->move_y < 0 ? IN_UP : 0) | (pi->move_y > 0 ? IN_DOWN : 0) | (pi->fire ? IN_FIRE : 0));
    }
    bool coin[2] = { in->coin, in->coin2 };
    for (int i = 0; i < 2; i++) {
        if (coin[i] && !g.coin_held[i]) g.coin_pulse[i] = COIN_PULSE_FRAMES;
        g.coin_held[i] = coin[i];
    }
    *sys = (uint8_t)((g.coin_pulse[0] > 0 ? SYS_COIN1 : 0) | (g.coin_pulse[1] > 0 ? SYS_COIN2 : 0) |
                     (in->start ? SYS_START1 : 0) | (in->start2 ? SYS_START2 : 0));
    for (int i = 0; i < 2; i++)
        if (g.coin_pulse[i] > 0) g.coin_pulse[i]--;
}

/* The game's picture into the arcade framebuffer */
static void draw_game(Framebuffer *t)
{
    static uint32_t screen[SF_W * SF_H];
    SfFrame fr;
    sf_frame_from_board(&fr, &g.board);
    sf_draw(&g.art, &fr, screen);
    for (int y = 0; y < FB_HEIGHT; y++) memcpy(fb_row(t, y), screen + y * SF_W, SF_W * sizeof screen[0]);
}

/* Classic: the arcade frame. HD: drawn at the render scale. FILTER: the arcade
   frame upscaled whole. Glow goes over whichever is shown. */
static void draw(int display_scale)
{
    const GfxSettings *gs = &g.gfx;
    const Scaler scaler = gs->smooth ? SCALER_SMOOTH : SCALER_CRISP;
    const int s = gfx_render_scale(gs, display_scale);
    shown = &fb;
    if (g.show != SHOW_CARD) { /* only pixels: HD upscales them like FILTER */
        draw_game(&fb);
        if (g.gfx_notice > 0) {
            char text[80];
            snprintf(text, sizeof text, "GRAPHICS: %s", gfx_describe(&g.gfx));
            fill_rectf(&fb, 0, FB_HEIGHT - 9, FB_WIDTH, 9, 0x000000);
            centred(&fb, FB_HEIGHT - 7, text, YELLOW);
        }
        if (gs->mode != GFX_CLASSIC && !fb_init(&fb_big, s) && !enhance_filter_frame(&fb, &fb_big, scaler))
            shown = &fb_big;
        if (gs->glow) enhance_glow(shown);
        return;
    }
    if (gs->mode == GFX_HD && !fb_init(&fb_big, s) && !enhance_prepare(s, scaler)) {
        draw_scene(&fb_big);
        shown = &fb_big;
    } else {
        draw_scene(&fb);
        if (gs->mode == GFX_FILTER && !fb_init(&fb_big, s) && !enhance_filter_frame(&fb, &fb_big, scaler))
            shown = &fb_big;
    }
    if (gs->glow) enhance_glow(shown);
}

static void step(const InputFrame *in)
{
    static bool coin_was, coin2_was;
    g.clock++;
    g.in = *in;
    if (in->coin && !coin_was) g.coins++;
    if (in->coin2 && !coin2_was) g.coins2++;
    coin_was = in->coin;
    coin2_was = in->coin2;
    if (g.show == SHOW_BOARD) {
        panel(in, &g.board.p1, &g.board.p2, &g.board.system);
        board_run_frame(&g.board);
        audio_stream(g.board.audio, g.board.audio_count, SOUND_RATE); /* the loop runs at the board's rate */
    }
    if (in->gfx_next || in->gfx_glow || in->gfx_scanlines) {
        if (in->gfx_next) gfx_next_preset(&g.gfx);
        if (in->gfx_glow) g.gfx.glow = !g.gfx.glow;
        if (in->gfx_scanlines) g.gfx.scanlines = !g.gfx.scanlines;
        g.gfx_notice = GFX_NOTICE_FRAMES;
    }
    if (g.gfx_notice > 0) g.gfx_notice--;
}

/* ---- tools ---------------------------------------------------------------- */

static int check_rom(const char *path)
{
    static RomSet rom;
    char err[512];
    if (rom_load(&rom, path, err, sizeof err)) {
        fprintf(stderr, "starforce: %s\n", err);
        return 1;
    }
    printf("starforce: %s: MAME starforc set OK\n", path);
    return 0;
}

static int dump_rom(const char *path, const char *only)
{
    static RomSet rom;
    char err[512];
    if (rom_load(&rom, path, err, sizeof err)) {
        fprintf(stderr, "starforce: %s\n", err);
        return 1;
    }
    int shown_any = 0;
    for (int r = 0; r < ROM_REGIONS; r++) {
        if (only && strcmp(only, rom_region_name(r))) continue;
        unsigned size;
        const uint8_t *p = rom_region(&rom, r, &size);
        for (unsigned i = 0; i < size; i++) printf("%s:%04X %02X\n", rom_region_name(r), i, p[i]);
        shown_any = 1;
    }
    if (!shown_any) {
        fprintf(stderr, "starforce: no region '%s'\n", only ? only : "");
        return 1;
    }
    return 0;
}

static void default_assets(const char *argv0, char *dir, size_t len)
{
    if (getenv("STARFORCE_ASSETS")) {
        snprintf(dir, len, "%s", getenv("STARFORCE_ASSETS"));
        return;
    }
    snprintf(dir, len, "%s", argv0);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (bslash > slash) slash = bslash;
#endif
    if (slash) snprintf(slash, len - (size_t)(slash - dir), "/assets");
    else snprintf(dir, len, "assets");
}

static int extract(const char *argv0, const char *rom_path, const char *out_dir)
{
    static RomSet rom;
    char err[512], dir[1024];
    if (out_dir) snprintf(dir, sizeof dir, "%s", out_dir);
    else default_assets(argv0, dir, sizeof dir);
    if (rom_load(&rom, rom_path, err, sizeof err) || extract_all(&rom, dir, err, sizeof err)) {
        fprintf(stderr, "starforce: %s\n", err);
        return 1;
    }
    printf("starforce: assets extracted to %s\n", dir);
    return 0;
}

static int save_screenshot(const char *path)
{
    static uint8_t rgba[FB_HEIGHT * FB_MAX_SCALE * FB_WIDTH * FB_MAX_SCALE * 4];
    const Framebuffer *f = shown ? shown : &fb;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++) {
            uint32_t p = fb_row(f, y)[x];
            uint8_t *o = rgba + ((long)y * f->w + x) * 4;
            o[0] = (uint8_t)(p >> 16);
            o[1] = (uint8_t)(p >> 8);
            o[2] = (uint8_t)p;
            o[3] = 0xFF;
        }
    if (png_write(path, rgba, f->w, f->h)) {
        fprintf(stderr, "starforce: cannot write %s\n", path);
        return -1;
    }
    return 0;
}

/* ---- loop ----------------------------------------------------------------- */

/* Loop state, static so the browser can run one pass per animation frame. */
static long max_frames = -1;
static const char *screenshot;
static Uint64 step_ticks, last, acc;
static long frames;
static int rc;

/* Step for the time elapsed, then draw and present; false = stop. */
static bool run_once(void)
{
    Uint64 now = SDL_GetPerformanceCounter();
    acc += now - last;
    last = now;
    if (acc > step_ticks * MAX_STEPS_PER_PRESENT)
        acc = step_ticks * MAX_STEPS_PER_PRESENT;
    /* Headless runs step once per loop so the frame count is exact. */
    if (max_frames >= 0 && acc < step_ticks) acc = step_ticks;

    long stepped = frames;
    while (acc >= step_ticks) {
        InputFrame in;
        input_poll(&in);
#ifndef __EMSCRIPTEN__
        if (in.quit) return false; /* a browser tab has nothing to quit to */
#endif
        step(&in);
        frames++;
        acc -= step_ticks;
        if (max_frames >= 0 && frames >= max_frames) break;
    }
    if (frames == stepped) {
#ifndef __EMSCRIPTEN__
        Uint64 ms = (step_ticks - acc) * 1000 / SDL_GetPerformanceFrequency();
        SDL_Delay(ms > 1 ? (Uint32)ms : 1);
#endif
        return true; /* the last picture stays */
    }
    if (max_frames < 0 || frames >= max_frames) draw(video_display_scale());
    if (max_frames < 0) video_present(shown, g.gfx.scanlines); /* blocks on vsync */
    if (max_frames >= 0 && frames >= max_frames) {
        if (screenshot && save_screenshot(screenshot)) rc = 1;
        return false;
    }
    return true;
}

#ifdef __EMSCRIPTEN__
static void web_frame(void) { run_once(); }
#endif

int main(int argc, char **argv)
{
    bool windowed = false, want_card = false;
    const char *rom_arg = NULL;
    if (argc == 3 && !strcmp(argv[1], "--check-rom")) return check_rom(argv[2]);
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "--dump-rom")) return dump_rom(argv[2], argc == 4 ? argv[3] : NULL);
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "--extract")) return extract(argv[0], argv[2], argc == 4 ? argv[3] : NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--windowed")) windowed = true;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot = argv[++i];
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_arg = argv[++i];
        else if (!strcmp(argv[i], "--test-card")) want_card = true;
        else if (!strcmp(argv[i], "--emulate")) {} /* the only game so far */
        else if (!strcmp(argv[i], "--gfx") && i + 1 < argc) {
            if (gfx_parse(argv[++i], &g.gfx)) {
                fprintf(stderr, "starforce: unknown --gfx preset '%s' (one of: %s)\n", argv[i], gfx_preset_names());
                return 2;
            }
        } else {
            fprintf(stderr,
                    "usage: %s [--windowed] [--gfx preset] [--rom path] [--test-card] [--emulate]\n"
                    "          [--frames N] [--screenshot out.png]\n"
                    "       %s --check-rom <dir|zip>\n"
                    "       %s --dump-rom <dir|zip> [region]\n"
                    "       %s --extract <dir|zip> [<asset dir>]\n",
                    argv[0], argv[0], argv[0], argv[0]);
            return 2;
        }
    }

    if (fb_init(&fb, 1)) {
        fprintf(stderr, "starforce: out of memory\n");
        return 1;
    }
    if (rom_arg) snprintf(g.rom_path, sizeof g.rom_path, "%s", rom_arg);
    else rom_locate(argv[0], g.rom_path, sizeof g.rom_path);
    if (g.rom_path[0] && rom_load(&g.rom, g.rom_path, g.rom_error, sizeof g.rom_error))
        fprintf(stderr, "starforce: %s\n", g.rom_error);
    if (assets_find(argv[0], "manifest.txt", g.assets_path, sizeof g.assets_path)) g.assets_path[0] = '\0';
    if (!g.assets_path[0] && g.rom.loaded && !want_card) { /* the first run: extract */
        char err[512];
        default_assets(argv[0], g.assets_path, sizeof g.assets_path);
        if (extract_all(&g.rom, g.assets_path, err, sizeof err)) { /* the program's folder is read-only: the save folder */
            snprintf(g.assets_path, sizeof g.assets_path, "%s", save_path("assets"));
            if (extract_all(&g.rom, g.assets_path, err, sizeof err)) {
                fprintf(stderr, "starforce: cannot extract the assets: %s\n", err);
                g.assets_path[0] = '\0';
            }
        }
        if (g.assets_path[0]) printf("starforce: assets extracted to %s\n", g.assets_path);
    }
    if (g.rom.loaded && !want_card) {
        g.show = SHOW_BOARD;
        sf_gfx_from_rom(&g.art, &g.rom);
        board_init(&g.board, &g.rom);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
#ifdef __EMSCRIPTEN__
    windowed = true; /* the page's canvas */
#endif
    if (!video_init(windowed)) {
        SDL_Quit();
        return 1;
    }
    input_init();
    if (max_frames < 0) audio_init(argv[0]); /* headless runs stay silent */

    step_ticks = (Uint64)(SDL_GetPerformanceFrequency() / FRAME_HZ);
    last = SDL_GetPerformanceCounter();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(web_frame, 0, 1); /* never returns */
#endif
    while (run_once()) {}

    audio_shutdown();
    input_shutdown();
    video_shutdown();
    SDL_Quit();
    enhance_clear_images();
    fb_free(&fb_big);
    fb_free(&fb);
    return rc;
}
