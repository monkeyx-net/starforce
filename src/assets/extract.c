#include "assets/extract.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

#include "assets/png.h"
#include "emu/board.h"

/* The programs' bytes the game in C reads, by address (tables.txt). */
static const struct {
    unsigned first, last;
    const char *what;
} tables[] = {
    /* whole: the programs read their code as data too, and the C game
       reads what they read */
    { 0x0000, 0x7FFF, "main: tables and code" },
    { EXTRACT_SUB_BASE + 0x0000, EXTRACT_SUB_BASE + 0x1FFF, "sub: the sound driver's tables and code" },
};
#define TABLES ((int)(sizeof tables / sizeof tables[0]))

/* the attract mode's screens for the previews (the emulated board's frames) */
static const struct {
    long frame;
    const char *name;
} screens[] = { { 300, "title" }, { 650, "demo" } };
#define SCREENS ((int)(sizeof screens / sizeof screens[0]))

/* ---- files ----------------------------------------------------------------- */

static int mkdir_p(const char *path)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(tmp, 0755) && errno != EEXIST ? -1 : 0;
}

/* Write `size` bytes to <dir>/<name> through a temp file, so a failure leaves no partial file. */
static int write_file(const char *dir, const char *name, const void *data, size_t size, char *err, int errlen)
{
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f || fwrite(data, 1, size, f) != size || fclose(f)) {
        if (f) fclose(f);
        remove(tmp);
        snprintf(err, (size_t)errlen, "cannot write %s", path);
        return -1;
    }
#ifdef _WIN32
    remove(path); /* Windows rename() does not replace */
#endif
    if (rename(tmp, path)) {
        remove(tmp);
        snprintf(err, (size_t)errlen, "cannot replace %s", path);
        return -1;
    }
    return 0;
}

/* A growing text buffer. */
typedef struct {
    char *s;
    size_t len, cap;
} Text;

static void put(Text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Text *t, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(t->s ? t->s + t->len : NULL, t->s ? t->cap - t->len : 0, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (t->s && t->len + (size_t)n < t->cap) {
            t->len += (size_t)n;
            return;
        }
        size_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap < t->len + (size_t)n + 1) cap *= 2;
        char *s = realloc(t->s, cap);
        if (!s) abort();
        t->s = s;
        t->cap = cap;
    }
}

static int write_text(const char *dir, const char *name, Text *t, char *err, int errlen)
{
    int rc = write_file(dir, name, t->s ? t->s : "", t->len, err, errlen);
    free(t->s);
    memset(t, 0, sizeof *t);
    return rc;
}

/* An RGB picture (0xRRGGBB) as a PNG under <dir>/gfx. */
static int write_png(const char *dir, const char *name, const uint32_t *px, int w, int h, char *err, int errlen)
{
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        snprintf(err, (size_t)errlen, "out of memory");
        return -1;
    }
    for (long i = 0; i < (long)w * h; i++) {
        rgba[i * 4] = (uint8_t)(px[i] >> 16);
        rgba[i * 4 + 1] = (uint8_t)(px[i] >> 8);
        rgba[i * 4 + 2] = (uint8_t)px[i];
        rgba[i * 4 + 3] = 0xFF;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/gfx/%s", dir, name);
    int rc = png_write(path, rgba, w, h);
    free(rgba);
    if (rc) snprintf(err, (size_t)errlen, "cannot write %s", path);
    return rc ? -1 : 0;
}

/* ---- graphics ---------------------------------------------------------------- */

static SfGfx gfx;

/* A preview's colours: the eight pens in grey. */
static uint32_t grey(int pen) { return (uint32_t)(pen * 36) * 0x010101u; }

static void put_tiles(Text *t, const char *what, const uint8_t *tiles, int n, int w)
{
    put(t, "# %s: %d rows of %d pens\n", what, w, w);
    for (int k = 0; k < n; k++) {
        put(t, "tile %03X\n", k);
        for (int y = 0; y < w; y++) {
            for (int x = 0; x < w; x++) put(t, "%d", tiles[(k * w + y) * w + x]);
            put(t, "\n");
        }
    }
}

/* a tile set as the player sees it (turned), 16 tiles a row */
static int preview(const char *dir, const char *name, const uint8_t *tiles, int n, int w, char *err, int errlen)
{
    int rows = (n + 15) / 16, pw = 16 * w, ph = rows * w;
    uint32_t *px = calloc((size_t)pw * (size_t)ph, sizeof *px);
    if (!px) {
        snprintf(err, (size_t)errlen, "out of memory");
        return -1;
    }
    for (int k = 0; k < n; k++)
        for (int y = 0; y < w; y++)
            for (int x = 0; x < w; x++) /* screen x = w - 1 - native y, screen y = native x */
                px[((k / 16) * w + x) * pw + (k % 16) * w + (w - 1 - y)] = grey(tiles[(k * w + y) * w + x]);
    int rc = write_png(dir, name, px, pw, ph, err, errlen);
    free(px);
    return rc;
}

static int extract_graphics(const char *dir, char *err, int errlen)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/gfx", dir);
    Text t = {0};
    put_tiles(&t, "the 512 fg tiles of 7.2fh, 8.3fh, 9.3fh", &gfx.fg[0][0], SF_FG_TILES, 8);
    if (write_text(path, "fg.txt", &t, err, errlen)) return -1;
    put_tiles(&t, "the 256 bg1 tiles of 15.10jk, 14.9jk, 13.8jk", &gfx.bg1[0][0], SF_BG_TILES, 16);
    if (write_text(path, "bg1.txt", &t, err, errlen)) return -1;
    put_tiles(&t, "the 256 bg2 tiles of 12.10de, 11.9de, 10.8de", &gfx.bg2[0][0], SF_BG_TILES, 16);
    if (write_text(path, "bg2.txt", &t, err, errlen)) return -1;
    put_tiles(&t, "the 128 bg3 tiles of 18.10pq, 17.9pq, 16.8pq", &gfx.bg3[0][0], SF_BG3_TILES, 16);
    if (write_text(path, "bg3.txt", &t, err, errlen)) return -1;
    put_tiles(&t, "the 512 16x16 sprites of 6.10lm, 5.9lm, 4.8lm", &gfx.spr[0][0], SF_SPRITES, 16);
    if (write_text(path, "sprites.txt", &t, err, errlen)) return -1;
    return preview(dir, "fg.png", &gfx.fg[0][0], SF_FG_TILES, 8, err, errlen) ||
           preview(dir, "bg1.png", &gfx.bg1[0][0], SF_BG_TILES, 16, err, errlen) ||
           preview(dir, "bg2.png", &gfx.bg2[0][0], SF_BG_TILES, 16, err, errlen) ||
           preview(dir, "bg3.png", &gfx.bg3[0][0], SF_BG3_TILES, 16, err, errlen) ||
           preview(dir, "sprites.png", &gfx.spr[0][0], SF_SPRITES, 16, err, errlen) ? -1 : 0;
}

static int load_tiles(const char *dir, const char *name, uint8_t *tiles, int n, int w)
{
    char path[1200], line[256];
    snprintf(path, sizeof path, "%s/gfx/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int k = -1, y = 0, rows = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned v;
        if (line[0] == '#') continue;
        if (sscanf(line, "tile %x", &v) == 1) {
            k = (int)v, y = 0;
            continue;
        }
        if (k < 0 || k >= n || y >= w) continue;
        for (int x = 0; x < w; x++) {
            if (line[x] < '0' || line[x] > '7') {
                fclose(f);
                return -1;
            }
            tiles[(k * w + y) * w + x] = (uint8_t)(line[x] - '0');
        }
        y++, rows++;
    }
    fclose(f);
    return rows == n * w ? 0 : -1;
}

int extract_load_gfx(const char *dir, SfGfx *g)
{
    return load_tiles(dir, "fg.txt", &g->fg[0][0], SF_FG_TILES, 8) ||
           load_tiles(dir, "bg1.txt", &g->bg1[0][0], SF_BG_TILES, 16) ||
           load_tiles(dir, "bg2.txt", &g->bg2[0][0], SF_BG_TILES, 16) ||
           load_tiles(dir, "bg3.txt", &g->bg3[0][0], SF_BG3_TILES, 16) ||
           load_tiles(dir, "sprites.txt", &g->spr[0][0], SF_SPRITES, 16) ? -1 : 0;
}

/* The game's characters: its messages in ASCII (a space $20); its status
   line's own, PLAYER, HI-SCORE and the digits from $0C (its 8 and 9 are
   $20 and $21: $20 left the space, the messages' colours hiding its 8) */
static int extract_font(const char *dir, char *err, int errlen)
{
    static const char status[] = "PLAYERHI-SCO01234567";
    Text t = {0};
    put(&t, "# the game's characters: fg tile, character\n");
    for (int c = '0'; c <= '9'; c++) put(&t, "%02X %c\n", c, c);
    for (int c = 'A'; c <= 'Z'; c++) put(&t, "%02X %c\n", c, c);
    put(&t, "20 space\n");
    for (int i = 0; status[i]; i++) put(&t, "%02X %c\n", 0x0C + i, status[i]);
    char gfx_dir[1100];
    snprintf(gfx_dir, sizeof gfx_dir, "%s/gfx", dir);
    return write_text(gfx_dir, "font.txt", &t, err, errlen);
}

/* ---- sound ------------------------------------------------------------------- */

static int extract_wave(const RomSet *rom, const char *dir, char *err, int errlen)
{
    char sound[1100];
    snprintf(sound, sizeof sound, "%s/sound", dir);
    if (mkdir_p(sound)) {
        snprintf(err, (size_t)errlen, "cannot create %s", sound);
        return -1;
    }
    Text t = {0};
    put(&t, "# the DAC's waveform (07b.bin): 32 bytes\nwave");
    for (int i = 0; i < ROM_DAC_SIZE; i++) put(&t, " %02X", rom->dac[i]);
    put(&t, "\n");
    return write_text(sound, "wave.txt", &t, err, errlen);
}

int extract_load_wave(const char *dir, uint8_t *wave)
{
    char path[1200], line[256];
    snprintf(path, sizeof path, "%s/sound/wave.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int got = 0;
    while (fgets(line, sizeof line, f)) {
        int k;
        if (strncmp(line, "wave", 4)) continue;
        const char *p = line + 4;
        unsigned v;
        while (got < ROM_DAC_SIZE && sscanf(p, "%x%n", &v, &k) == 1) wave[got++] = (uint8_t)v, p += k;
    }
    fclose(f);
    return got == ROM_DAC_SIZE ? 0 : -1;
}

/* ---- tables ------------------------------------------------------------------ */

static int table_byte(const RomSet *rom, unsigned a)
{
    return a < EXTRACT_SUB_BASE ? rom->main[a] : rom->sub[a - EXTRACT_SUB_BASE];
}

int extract_is_table(unsigned addr)
{
    for (int i = 0; i < TABLES; i++)
        if (tables[i].what && addr >= tables[i].first && addr <= tables[i].last) return 1;
    return 0;
}

static int extract_tables(const RomSet *rom, const char *dir, char *err, int errlen)
{
    Text t = {0};
    put(&t, "# the programs' tables the game in C reads (main; sub +$8000)\n");
    for (int i = 0; i < TABLES; i++) {
        if (!tables[i].what) continue;
        put(&t, "# %s\n", tables[i].what);
        for (unsigned a = tables[i].first; a <= tables[i].last; a += 16) {
            put(&t, "%04X:", a);
            for (unsigned b = a; b < a + 16 && b <= tables[i].last; b++) put(&t, " %02X", table_byte(rom, b));
            put(&t, "\n");
        }
    }
    return write_text(dir, "tables.txt", &t, err, errlen);
}

int extract_load_tables(const char *dir, uint8_t *data, unsigned size)
{
    char path[1200], line[256];
    snprintf(path, sizeof path, "%s/tables.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(data, 0, size);
    unsigned n = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned a, v;
        int k;
        if (line[0] == '#' || sscanf(line, "%x:%n", &a, &k) != 1) continue;
        for (const char *p = line + k; sscanf(p, "%x%n", &v, &k) == 1; p += k, a++)
            if (a < size) data[a] = (uint8_t)v, n++;
    }
    fclose(f);
    unsigned want = 0;
    for (int i = 0; i < TABLES; i++)
        if (tables[i].what) want += tables[i].last - tables[i].first + 1;
    return n == want ? 0 : -1;
}

/* ---- the attract mode's screens ------------------------------------------------ */

static Board board;

static int extract_screens(const RomSet *rom, const char *dir, char *err, int errlen)
{
    board_init(&board, rom);
    static uint32_t px[SF_W * SF_H];
    int rc = 0;
    for (int i = 0; i < SCREENS && !rc; i++) {
        while (board.frame < screens[i].frame) board_run_frame(&board);
        SfFrame fr;
        sf_frame_from_board(&fr, &board);
        sf_draw(&gfx, &fr, px);
        for (int k = 0; k < SF_W * SF_H; k++) px[k] &= 0xFFFFFF;
        char name[64];
        snprintf(name, sizeof name, "%s.png", screens[i].name);
        rc = write_png(dir, name, px, SF_W, SF_H, err, errlen);
    }
    return rc;
}

int extract_all(const RomSet *rom, const char *out_dir, char *err, int errlen)
{
    char path[1100];
    if (!rom->loaded) {
        snprintf(err, (size_t)errlen, "no ROM set loaded");
        return -1;
    }
    snprintf(path, sizeof path, "%s/%s", out_dir, EXTRACT_MANIFEST);
    remove(path); /* until the end, the folder is incomplete */
    snprintf(path, sizeof path, "%s/gfx", out_dir);
    if (mkdir_p(path)) {
        snprintf(err, (size_t)errlen, "cannot create %s", path);
        return -1;
    }
    sf_gfx_from_rom(&gfx, rom);
    if (extract_graphics(out_dir, err, errlen) || extract_font(out_dir, err, errlen) ||
        extract_wave(rom, out_dir, err, errlen) || extract_tables(rom, out_dir, err, errlen) ||
        extract_screens(rom, out_dir, err, errlen))
        return -1;
    Text t = {0};
    put(&t, "starforce assets %d\n", EXTRACT_VERSION);
    return write_text(out_dir, EXTRACT_MANIFEST, &t, err, errlen);
}
