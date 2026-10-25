/*
 * Star Force native build: SDL setup and the fixed-rate loop (the board's
 * 59.19 Hz). See PROGRESS_STARFORCE_PORT.md.
 * The game in C (src/sim/) runs a frame per tick from the assets extracted
 * from the player's set (extracted on the first run when the set is found),
 * its sound streamed; with --emulate the ROM's own code runs on the
 * emulated board instead (src/emu/). Without either, or with --test-card,
 * the test card (ROM status, an input tester and the graphics presets).
 *
 *   starforce [--windowed] [--gfx preset] [--rom path] [--test-card] [--emulate]
 *          [--input script.txt] [--frames N] [--screenshot out.png]
 *   starforce --check-rom <dir|zip>
 *   starforce --dump-rom <dir|zip> [region]
 *   starforce --extract <dir|zip> [<asset dir>]
 *
 * --frames N stops after N updates; with --screenshot the last frame is
 * written as a PNG (use SDL_VIDEODRIVER=dummy to run headless).
 * --input replays scripted controls: lines of "<frame> [coin] [coin2]
 * [start] [start2] [left] [right] [up] [down] [fire] [menu]" (player
 * one's), each holding from that frame until the next line ("menu" a press
 * of F2 on its first frame; live controls still quit).
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
#include "sim/sim.h"
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
#define SETTINGS_FILE "starforce.cfg"
#define HISCORE_FILE "starforce.hiscore"
#define HISCORE_SAVE_FRAMES 300 /* a changed table is written at most every 5 s */
/* The controls page, shown over the attract mode when nobody has touched the
   controls for CONTROLS_IDLE_FRAMES; it stays CONTROLS_SHOW_FRAMES and comes
   back every CONTROLS_IDLE_FRAMES + CONTROLS_SHOW_FRAMES. Any input hides it. */
#define CONTROLS_IDLE_FRAMES (30 * 60)
#define CONTROLS_SHOW_FRAMES (10 * 60)
/* The high scores kept (the arcade forgets them at power-off): the BEST
   PLAYERS table ($8038: bit 0 built; ten entries of 11 bytes from $8039,
   then $FF) and the top HI-SCORE ($8348-$834B, BCD, the low byte first),
   whose eight digits the game writes down the fg layer's column 1 from
   $9261 (leading zeros left out), each from the ROM's digits at $56BC */
#define HISCORE_TABLE 0x038 /* in the RAM at $8000 */
#define HISCORE_TABLE_LEN 112
#define HISCORE_TOP 0x348
#define HISCORE_TOP_LEN 4
#define HISCORE_LEN (HISCORE_TABLE_LEN + HISCORE_TOP_LEN)
#define HISCORE_DIGITS_AT 0x261 /* in the fg codes */
#define HISCORE_DIGIT_TILES 0x56BC /* in main's ROM */
#define FG_BLANK 0x23

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
static const uint32_t *glow; /* the glow for the GPU to add over it, or NULL (added already, or none) */

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
    enum { SHOW_CARD, SHOW_GAME, SHOW_BOARD } show; /* the test card, the C game, the board */
    Board board;
    Sim sim;
    SfGfx art;              /* the graphics, from the assets (the C game) or the ROM set (the board) */
    uint8_t data[GAME_DATA_SIZE]; /* the programs' tables, from the assets */
    uint8_t wave[ROM_DAC_SIZE];   /* the DAC's waveform, from the assets */
    int coin_pulse[2];      /* frames left of each coin switch's pulse */
    bool coin_held[2];
    uint8_t dsw1, dsw2;     /* the DIP switches (starforce.cfg) */
    long idle;              /* frames of attract mode without input (the controls page) */
    uint8_t hiscore[HISCORE_LEN]; /* the high scores as last saved or placed */
    int hiscore_saved;      /* starforce.hiscore read or written */
    int hiscore_placed;     /* put into the game's RAM after its power-on */
    int hiscore_wait;       /* frames the table has differed from the saved one */
    int menu;               /* the operator menu is open: the game waits */
    int menu_row;
    uint8_t menu_dsw1;      /* DSW1 when it opened */
    InputFrame menu_was;    /* the controls last tick, for the menu's presses */
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

/* The board's memory, the C game's or the emulated board's */
static uint8_t *game_ram(void) { return g.show == SHOW_BOARD ? g.board.ram : g.sim.g.ram; }
static uint8_t *game_fg(void) { return g.show == SHOW_BOARD ? g.board.fgvideo : g.sim.g.fgvideo; }
/* a byte of main's ROM (the C game's from the assets) */
static uint8_t game_rom(unsigned a) { return g.show == SHOW_BOARD ? g.rom.main[a] : g.data[a]; }
/* a game is on ($8018 bit 0) */
static int game_on(void) { return game_ram()[0x018] & 1; }

/* Power on, with the switches */
static void power_on(void)
{
    if (g.show == SHOW_BOARD) {
        board_init(&g.board, &g.rom);
        g.board.dsw1 = g.dsw1;
        g.board.dsw2 = g.dsw2;
    } else if (g.show == SHOW_GAME) {
        sim_init(&g.sim, g.data, g.dsw1, g.dsw2);
        sim_sound(&g.sim, g.wave);
    }
    g.hiscore_placed = 0;
}

/* The DIP switches (MAME's ports DSW1, DSW2), as MAME's defaults unless
   starforce.cfg says otherwise. The file is written with the defaults if
   missing, and again when the operator menu closes. */
#define DIP_ROWS 7

static const struct {
    const char *what, *key;
    int sw, mask, n;            /* DSW1 (0) or DSW2 (1), its bits, its settings */
    uint8_t values[8];
    const char *tokens[8];      /* in starforce.cfg */
    const char *names[8];       /* in the menu */
} DIPS[DIP_ROWS] = {
    { "COIN A", "coin_a", 0, 0x03, 4, { 0x01, 0x00, 0x02, 0x03 }, { "2c1c", "1c1c", "1c2c", "1c3c" },
      { "2 COINS 1 PLAY", "1 COIN 1 PLAY", "1 COIN 2 PLAYS", "1 COIN 3 PLAYS" } },
    { "COIN B", "coin_b", 0, 0x0C, 4, { 0x04, 0x00, 0x08, 0x0C }, { "2c1c", "1c1c", "1c2c", "1c3c" },
      { "2 COINS 1 PLAY", "1 COIN 1 PLAY", "1 COIN 2 PLAYS", "1 COIN 3 PLAYS" } },
    { "SHIPS", "lives", 0, 0x30, 4, { 0x30, 0x00, 0x10, 0x20 }, { "2", "3", "4", "5" }, { "2", "3", "4", "5" } },
    { "BONUS SHIP", "bonus", 1, 0x07, 8, { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 },
      { "50k200k500k", "100k300k800k", "50k200k", "100k300k", "50k", "100k", "200k", "none" },
      { "50K 200K 500K", "100K 300K 800K", "50K AND 200K", "100K AND 300K", "50K ONLY", "100K ONLY", "200K ONLY",
        "NONE" } },
    { "DIFFICULTY", "difficulty", 1, 0x38, 6, { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28 },
      { "easiest", "easy", "medium", "difficult", "hard", "hardest" },
      { "EASIEST", "EASY", "MEDIUM", "DIFFICULT", "HARD", "HARDEST" } },
    { "DEMO SOUNDS", "demo_sounds", 0, 0x80, 2, { 0x80, 0x00 }, { "on", "off" }, { "ON", "OFF" } },
    { "CABINET", "cabinet", 0, 0x40, 2, { 0x40, 0x00 }, { "upright", "cocktail" }, { "UPRIGHT", "COCKTAIL" } },
};

static int dip_get(int row)
{
    uint8_t v = (DIPS[row].sw ? g.dsw2 : g.dsw1) & DIPS[row].mask;
    for (int i = 0; i < DIPS[row].n; i++)
        if (DIPS[row].values[i] == v) return i;
    return 0;
}

static void dip_set(int row, int i)
{
    uint8_t *sw = DIPS[row].sw ? &g.dsw2 : &g.dsw1;
    *sw = (uint8_t)((*sw & ~DIPS[row].mask) | DIPS[row].values[i]);
}

static void settings_save(void)
{
    char text[1024];
    int n = snprintf(text, sizeof text,
                     "# Star Force's DIP switches (MAME's defaults: 1c1c 1c1c 3 50k200k500k easiest on upright;"
                     " F2 in the game)\n");
    for (int r = 0; r < DIP_ROWS && n < (int)sizeof text; r++) {
        n += snprintf(text + n, sizeof text - (size_t)n, "%s %s\n#", DIPS[r].key, DIPS[r].tokens[dip_get(r)]);
        for (int i = 0; i < DIPS[r].n && n < (int)sizeof text; i++)
            n += snprintf(text + n, sizeof text - (size_t)n, " %s", DIPS[r].tokens[i]);
        if (n < (int)sizeof text) n += snprintf(text + n, sizeof text - (size_t)n, "\n");
    }
    if (n >= (int)sizeof text) n = (int)sizeof text - 1;
    if (save_store(SETTINGS_FILE, text, (size_t)n)) fprintf(stderr, "starforce: cannot save %s\n", save_path(SETTINGS_FILE));
}

static void settings_load(void)
{
    g.dsw1 = DSW1_DEFAULT;
    g.dsw2 = DSW2_DEFAULT;
    FILE *f = fopen(save_path(SETTINGS_FILE), "r");
    if (!f) {
        settings_save();
        return;
    }
    char line[256], key[32], value[32];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%31s %31s", key, value) != 2) continue;
        for (int r = 0; r < DIP_ROWS; r++)
            if (!strcmp(key, DIPS[r].key))
                for (int i = 0; i < DIPS[r].n; i++)
                    if (!strcmp(value, DIPS[r].tokens[i])) dip_set(r, i);
    }
    fclose(f);
}

/* ---- the high scores ------------------------------------------------------------ */

/* Main's power-on sets the top score from its ROM (50800, again once its
   RAM is cleared), draws the status line from a ROM template holding
   50800 too ($0545, at frame 165), and builds the table the first time it
   shows it (bit 0 of $8038 clear); the saved table goes in once the
   template is drawn and before the table is built, its top score at the
   top, drawn as the game draws it. Without a saved one, the game's own table is taken once built.
   A changed table is saved (at most every HISCORE_SAVE_FRAMES). */
static void hiscore_read(uint8_t *out)
{
    const uint8_t *ram = game_ram();
    memcpy(out, ram + HISCORE_TABLE, HISCORE_TABLE_LEN);
    memcpy(out + HISCORE_TABLE_LEN, ram + HISCORE_TOP, HISCORE_TOP_LEN);
}

/* the top score's digits as the game writes them */
static void hiscore_draw_top(void)
{
    const uint8_t *top = game_ram() + HISCORE_TOP;
    uint8_t *fg = game_fg();
    int started = 0;
    for (int k = 0; k < 2 * HISCORE_TOP_LEN; k++) {
        uint8_t byte = top[HISCORE_TOP_LEN - 1 - k / 2];
        int d = k & 1 ? byte & 15 : byte >> 4;
        started |= d != 0;
        fg[HISCORE_DIGITS_AT - 0x20 * k] = started ? game_rom(HISCORE_DIGIT_TILES + (unsigned)d) : FG_BLANK;
    }
}

/* the status line shows the template's 50800 */
static int template_top_shown(void)
{
    const uint8_t *fg = game_fg();
    for (int k = 0; k < 8; k++) {
        int d = "00050800"[k] - '0';
        uint8_t want = k < 3 ? FG_BLANK : game_rom(HISCORE_DIGIT_TILES + (unsigned)d);
        if (fg[HISCORE_DIGITS_AT - 0x20 * k] != want) return 0;
    }
    return 1;
}

static void hiscore_step(void)
{
    static const uint8_t rom_top[HISCORE_TOP_LEN] = { 0x00, 0x08, 0x05, 0x00 };
    uint8_t *ram = game_ram();
    if (!g.hiscore_placed) {
        if (g.hiscore_saved) {
            /* the top score set from the ROM (50800) and the template drawn, the
               table not built yet */
            if (memcmp(ram + HISCORE_TOP, rom_top, HISCORE_TOP_LEN) || (ram[HISCORE_TABLE] & 1) || !template_top_shown())
                return;
            memcpy(ram + HISCORE_TABLE, g.hiscore, HISCORE_TABLE_LEN);
            memcpy(ram + HISCORE_TOP, g.hiscore + HISCORE_TABLE_LEN, HISCORE_TOP_LEN);
            hiscore_draw_top();
        } else {
            if (!(ram[HISCORE_TABLE] & 1)) return;
            hiscore_read(g.hiscore);
        }
        g.hiscore_placed = 1;
        return;
    }
    /* the game draws the template again on each new screen: the top score
       it holds over it */
    if (memcmp(ram + HISCORE_TOP, rom_top, HISCORE_TOP_LEN) && template_top_shown()) hiscore_draw_top();
    uint8_t now[HISCORE_LEN];
    hiscore_read(now);
    if (!memcmp(now, g.hiscore, HISCORE_LEN)) {
        g.hiscore_wait = 0;
        return;
    }
    if (++g.hiscore_wait < HISCORE_SAVE_FRAMES) return;
    memcpy(g.hiscore, now, HISCORE_LEN);
    g.hiscore_saved = 1;
    g.hiscore_wait = 0;
    if (save_store(HISCORE_FILE, g.hiscore, HISCORE_LEN)) fprintf(stderr, "starforce: cannot save %s\n", save_path(HISCORE_FILE));
}

/* ---- the operator menu: the DIP switches ------------------------------------------------------- */

/* F2 or Select+R1 opens it over the game, which waits; up/down choose a
   switch, left/right (or fire) change it, F2 or Select+R1 again saves
   starforce.cfg and goes on. The game reads the switches as the board
   does; it reads the coinage only at power-on, so a new coinage powers the
   game on again. */
static void menu_step(const InputFrame *in)
{
    const InputFrame *was = &g.menu_was;
    if (in->move_y && in->move_y != was->move_y) g.menu_row = (g.menu_row + in->move_y + DIP_ROWS) % DIP_ROWS;
    int dx = in->move_x && in->move_x != was->move_x ? in->move_x : 0;
    bool fire = in->p[0].fire || in->p[1].fire, fire_was = was->p[0].fire || was->p[1].fire;
    if (fire && !fire_was) dx = 1;
    if (dx) {
        int n = DIPS[g.menu_row].n;
        dip_set(g.menu_row, (dip_get(g.menu_row) + dx + n) % n);
    }
    g.menu_was = *in;
}

static void menu_close(void)
{
    settings_save();
    if ((g.dsw1 & 0x0F) != (g.menu_dsw1 & 0x0F)) {
        power_on();
        return;
    }
    if (g.show == SHOW_BOARD) {
        g.board.dsw1 = g.dsw1;
        g.board.dsw2 = g.dsw2;
    } else {
        g.sim.g.dsw1 = g.dsw1;
        g.sim.g.dsw2 = g.dsw2;
    }
}

static void draw_menu(Framebuffer *t)
{
    const int x0 = 4, y0 = 44, w = FB_WIDTH - 8, h = 160;
    fill_rectf(t, x0, y0, w, h, 0x000000);
    fill_rectf(t, x0, y0, w, 1, ORANGE);
    fill_rectf(t, x0, y0 + h - 1, w, 1, ORANGE);
    centred(t, y0 + 8, "STAR FORCE  -  OPERATOR MENU", YELLOW);
    int y = y0 + 26;
    for (int r = 0; r < DIP_ROWS; r++, y += 12) {
        int on = r == g.menu_row;
        if (on) debug_text(t, x0 + 4, y, ">", YELLOW);
        debug_text(t, x0 + 12, y, DIPS[r].what, on ? YELLOW : WHITE);
        debug_text(t, x0 + 76, y, DIPS[r].names[dip_get(r)], on ? YELLOW : GREEN);
    }
    centred(t, y0 + h - 40, "UP/DOWN CHOOSE   LEFT/RIGHT CHANGE", GREY);
    centred(t, y0 + h - 30, "A NEW COINAGE STARTS THE GAME AGAIN", GREY);
    centred(t, y0 + h - 14, "F2 / SELECT+R1: SAVE AND PLAY", CYAN);
}

/* ---- the controls page ---------------------------------------------------------- */

static bool any_input(const InputFrame *in)
{
    for (int i = 0; i < 2; i++)
        if (in->p[i].move_x || in->p[i].move_y || in->p[i].fire) return true;
    return in->coin || in->coin2 || in->start || in->start2;
}

static bool controls_page_up(void)
{
    if (g.show == SHOW_CARD || g.menu || g.idle < CONTROLS_IDLE_FRAMES) return false;
    return (g.idle - CONTROLS_IDLE_FRAMES) % (CONTROLS_IDLE_FRAMES + CONTROLS_SHOW_FRAMES) < CONTROLS_SHOW_FRAMES;
}

static const struct { const char *what, *pad, *keys; } CONTROLS[] = {
    { "MOVE", "STICK, D-PAD", "ARROWS" },
    { "FIRE", "A, B", "SPACE, CTRL" },
    { "COIN", "SELECT", "5 (6)" },
    { "1 PLAYER", "START", "1, ENTER" },
    { "2 PLAYERS", "L1+START", "2" },
    { "GRAPHICS", "SELECT+L1", "F3" },
    { "GLOW, CRT", "", "F4, F5" },
    { "OPERATOR MENU", "SELECT+R1", "F2" },
    { "EXIT", "SELECT+START", "ESC" },
};

/* Over the game's frame (cosmetic: the next frame is the arcade's again). */
static void draw_controls(Framebuffer *t)
{
    const int x0 = 8, y0 = 50, w = FB_WIDTH - 16, h = 150;
    fill_rectf_alpha(t, x0, y0, w, h, 0x000000, 0.94);
    fill_rectf(t, x0, y0, w, 1, ORANGE);
    fill_rectf(t, x0, y0 + h - 1, w, 1, ORANGE);
    centred(t, y0 + 8, "STAR FORCE  -  CONTROLS", YELLOW);
    int y = y0 + 22;
    debug_text(t, x0 + 74, y, "PAD", CYAN);
    debug_text(t, x0 + 140, y, "KEYBOARD", CYAN);
    y += 12;
    for (size_t i = 0; i < sizeof CONTROLS / sizeof CONTROLS[0]; i++, y += 9) {
        debug_text(t, x0 + 6, y, CONTROLS[i].what, WHITE);
        debug_text(t, x0 + 74, y, CONTROLS[i].pad, GREEN);
        debug_text(t, x0 + 140, y, CONTROLS[i].keys, GREEN);
    }
    centred(t, y0 + h - 26, "TWO PADS: ONE A PLAYER", GREY);
    centred(t, y0 + h - 12, "CONVERTED BY MONKEYX", CYAN);
}

/* The game's picture into the arcade framebuffer */
static void draw_game(Framebuffer *t)
{
    static uint32_t screen[SF_W * SF_H];
    SfFrame fr;
    if (g.show == SHOW_BOARD) {
        sf_frame_from_board(&fr, &g.board);
    } else {
        const Game *m = &g.sim.g;
        fr = (SfFrame){ m->fgvideo, m->fgcolor, m->sprites, m->palette, m->regs, m->bg1, m->bg2, m->bg3, m->flip };
    }
    sf_draw(&g.art, &fr, screen);
    for (int y = 0; y < FB_HEIGHT; y++) memcpy(fb_row(t, y), screen + y * SF_W, SF_W * sizeof screen[0]);
}

/* The glow: computed small here (from `from`, the arcade frame where the
   shown one is it upscaled), stretched and added by the GPU where it can
   (headless runs and software renderers add it here, to every pixel) */
static long max_frames = -1;
static void add_glow(const GfxSettings *gs, const Framebuffer *from)
{
    glow = NULL;
    if (!gs->glow) return;
    if (max_frames < 0 && video_gpu_glow()) glow = enhance_glow_grid(from);
    else enhance_glow(shown);
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
        if (controls_page_up()) draw_controls(&fb);
        if (g.menu) draw_menu(&fb);
        if (g.gfx_notice > 0) {
            char text[80];
            snprintf(text, sizeof text, "GRAPHICS: %s", gfx_describe(&g.gfx));
            fill_rectf(&fb, 0, FB_HEIGHT - 9, FB_WIDTH, 9, 0x000000);
            centred(&fb, FB_HEIGHT - 7, text, YELLOW);
        }
        if (gs->mode != GFX_CLASSIC && !fb_init(&fb_big, s) && !enhance_filter_frame(&fb, &fb_big, scaler))
            shown = &fb_big;
        add_glow(gs, &fb);
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
    add_glow(gs, shown);
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
    if (in->menu && g.show != SHOW_CARD) {
        if (g.menu) menu_close();
        else g.menu_dsw1 = g.dsw1;
        g.menu = !g.menu;
        g.menu_was = *in;
    }
    if (g.menu) { /* the game waits */
        static const int16_t quiet[811];
        menu_step(in);
        if (g.show != SHOW_CARD) audio_stream(quiet, 811, SOUND_RATE);
    } else if (g.show == SHOW_BOARD) {
        panel(in, &g.board.p1, &g.board.p2, &g.board.system);
        board_run_frame(&g.board);
        audio_stream(g.board.audio, g.board.audio_count, SOUND_RATE); /* the loop runs at the board's rate */
    } else if (g.show == SHOW_GAME) {
        uint8_t p1, p2, sys;
        panel(in, &p1, &p2, &sys);
        sim_frame(&g.sim, p1, p2, sys);
        audio_stream(g.sim.audio, g.sim.audio_count, SOUND_RATE);
    }
    if (g.show != SHOW_CARD && !g.menu) {
        hiscore_step();
        g.idle = game_on() || any_input(in) ? 0 : g.idle + 1;
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

/* ---- scripted input ------------------------------------------------------- */

typedef struct {
    long frame;
    InputFrame in;
} ScriptLine;

static ScriptLine script[4096];
static int script_len, script_pos;

static int load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "starforce: cannot open %s\n", path);
        return -1;
    }
    char line[256];
    while (script_len < (int)(sizeof script / sizeof script[0]) && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        ScriptLine *l = &script[script_len];
        memset(l, 0, sizeof *l);
        char *tok = strtok(line, " \t\n");
        if (!tok) continue;
        l->frame = strtol(tok, NULL, 10);
        PlayerInput *p = &l->in.p[0];
        while ((tok = strtok(NULL, " \t\n"))) {
            if (!strcmp(tok, "coin")) l->in.coin = true;
            else if (!strcmp(tok, "coin2")) l->in.coin2 = true;
            else if (!strcmp(tok, "start")) l->in.start = true;
            else if (!strcmp(tok, "start2")) l->in.start2 = true;
            else if (!strcmp(tok, "left")) p->move_x = -1;
            else if (!strcmp(tok, "right")) p->move_x = 1;
            else if (!strcmp(tok, "up")) p->move_y = -1;
            else if (!strcmp(tok, "down")) p->move_y = 1;
            else if (!strcmp(tok, "fire")) p->fire = true;
            else if (!strcmp(tok, "menu")) l->in.menu = true;
            else fprintf(stderr, "starforce: %s: unknown token '%s'\n", path, tok);
        }
        l->in.p[1] = *p;
        l->in.move_x = p->move_x;
        l->in.move_y = p->move_y;
        script_len++;
    }
    fclose(f);
    return 0;
}

/* Replace live controls with the script line in force at `frame` (quit stays live). */
static void apply_script(InputFrame *in, long frame)
{
    while (script_pos + 1 < script_len && script[script_pos + 1].frame <= frame) script_pos++;
    bool quit = in->quit;
    if (script[script_pos].frame <= frame) *in = script[script_pos].in;
    else memset(in, 0, sizeof *in);
    in->menu = in->menu && script[script_pos].frame == frame; /* a press, on the line's first frame */
    in->quit = quit;
}

/* ---- loop ----------------------------------------------------------------- */

/* Loop state, static so the browser can run one pass per animation frame. */
static const char *screenshot;
static Uint64 step_ticks, last, acc;
static long frames;
static int rc;

/* Step for the time elapsed, then draw and present; false = stop. */
/* STARFORCE_PROFILE=1: the loop's time a frame (the game, drawing with the
   preset's effects, presenting), averaged and printed every 5 s */
static int profile;
static double prof_ms[3];
static long prof_frames;

static double ms_since(Uint64 t) { return (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

static void profile_report(void)
{
    if (!profile || ++prof_frames < 300) return;
    fprintf(stderr, "starforce: %s, %s: game %.2f ms, draw %.2f ms, present %.2f ms a frame\n",
            g.show == SHOW_BOARD ? "board" : g.show == SHOW_GAME ? "C game" : "test card", gfx_describe(&g.gfx),
            prof_ms[0] / prof_frames, prof_ms[1] / prof_frames, prof_ms[2] / prof_frames);
    prof_ms[0] = prof_ms[1] = prof_ms[2] = 0;
    prof_frames = 0;
}

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
        if (script_len) apply_script(&in, frames + 1);
        Uint64 t0 = SDL_GetPerformanceCounter();
        step(&in);
        prof_ms[0] += ms_since(t0);
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
    Uint64 t1 = SDL_GetPerformanceCounter();
    if (max_frames < 0 || frames >= max_frames) draw(video_display_scale());
    prof_ms[1] += ms_since(t1);
    t1 = SDL_GetPerformanceCounter();
    if (max_frames < 0) video_present(shown, g.gfx.scanlines, glow); /* blocks on vsync */
    prof_ms[2] += ms_since(t1);
    profile_report();
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
    bool windowed = false, want_card = false, want_board = false;
    profile = getenv("STARFORCE_PROFILE") && *getenv("STARFORCE_PROFILE") == '1';
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
        else if (!strcmp(argv[i], "--emulate")) want_board = true;
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) {
            if (load_script(argv[++i])) return 2;
        }
        else if (!strcmp(argv[i], "--gfx") && i + 1 < argc) {
            if (gfx_parse(argv[++i], &g.gfx)) {
                fprintf(stderr, "starforce: unknown --gfx preset '%s' (one of: %s)\n", argv[i], gfx_preset_names());
                return 2;
            }
        } else {
            fprintf(stderr,
                    "usage: %s [--windowed] [--gfx preset] [--rom path] [--test-card] [--emulate]\n"
                    "          [--input script.txt] [--frames N] [--screenshot out.png]\n"
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
    if (!g.assets_path[0] && g.rom.loaded && !want_card && !want_board) { /* the first run: extract */
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
    g.show = SHOW_CARD;
    if (want_board && !want_card) {
        if (!g.rom.loaded) {
            fprintf(stderr, "starforce: --emulate needs the ROM set\n");
        } else {
            g.show = SHOW_BOARD;
            sf_gfx_from_rom(&g.art, &g.rom);
        }
    } else if (!want_card && g.assets_path[0]) {
        if (extract_load_tables(g.assets_path, g.data, sizeof g.data) || extract_load_wave(g.assets_path, g.wave) ||
            extract_load_gfx(g.assets_path, &g.art)) {
            fprintf(stderr, "starforce: the assets in %s are incomplete: extract them again (--extract)\n", g.assets_path);
        } else {
            g.show = SHOW_GAME;
        }
    }
    if (g.show != SHOW_CARD) {
        settings_load();
        g.hiscore_saved = !save_load(HISCORE_FILE, g.hiscore, sizeof g.hiscore);
        power_on();
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
