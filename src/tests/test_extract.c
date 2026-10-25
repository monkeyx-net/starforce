/*
 * The extractor against the user's starforc set (label rom):
 *   test_extract <starforc.zip or folder> <scratch dir>
 * Two extractions are byte-identical and hold every file; the graphics read
 * back from them are the ROM set's, the waveform too, and the tables (the
 * ROMs' bytes in their ranges); the font reads the attract mode's screens
 * the emulated board draws ("GAME OVER", "INSERT COIN" in the messages'
 * characters, "HI-SCORE", "PLAYER1" in the status line's), each letter's
 * pixels on screen its tile's; and with no ROM set nothing is written, not
 * even a manifest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/extract.h"
#include "assets/rom.h"
#include "emu/board.h"
#include "gfx/starforce.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

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

static const char *const files[] = {
    "manifest.txt",   "gfx/fg.txt",  "gfx/bg1.txt", "gfx/bg2.txt", "gfx/bg3.txt",     "gfx/sprites.txt",
    "gfx/font.txt",   "gfx/fg.png",  "gfx/bg1.png", "gfx/bg2.png", "gfx/bg3.png",     "gfx/sprites.png",
    "gfx/title.png",  "gfx/demo.png", "sound/wave.txt", "tables.txt",
};
#define FILES ((int)(sizeof files / sizeof files[0]))

static RomSet rom, none;
static SfGfx from_rom, from_assets;
static Board board;

static long read_all(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc((size_t)n + 1);
    if (fread(*out, 1, (size_t)n, f) != (size_t)n) n = -1;
    fclose(f);
    return n;
}

static int same_file(const char *a_dir, const char *b_dir, const char *name)
{
    char pa[1200], pb[1200], *a = NULL, *b = NULL;
    snprintf(pa, sizeof pa, "%s/%s", a_dir, name);
    snprintf(pb, sizeof pb, "%s/%s", b_dir, name);
    long na = read_all(pa, &a), nb = read_all(pb, &b);
    int same = na >= 0 && na == nb && !memcmp(a, b, (size_t)na);
    free(a);
    free(b);
    return same;
}

/* The characters of the font file by tile code */
static void load_font(const char *dir, char *chars)
{
    char path[1200], line[256];
    memset(chars, 0, 256);
    snprintf(path, sizeof path, "%s/gfx/font.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        unsigned n;
        char c[16];
        if (line[0] == '#' || sscanf(line, "%x %15s", &n, c) != 2 || n > 255) continue;
        chars[n] = !strcmp(c, "space") ? ' ' : c[0];
    }
    fclose(f);
}

/* A row of text as the player reads it: native column `col` (the screen's
   row col), cells from native row 29 down to 2 ('?' where no character) */
static void row_text(const uint8_t *fgvideo, const char *chars, int col, char *out)
{
    for (int i = 0; i < 28; i++) {
        int code = fgvideo[(29 - i) * 32 + col];
        out[i] = chars[code] ? chars[code] : '?';
    }
    out[28] = 0;
}

/* Where a word is on the fg layer: its first cell, or -1 */
static int find_word(const uint8_t *fgvideo, const char *chars, const char *word)
{
    char text[29];
    for (int col = 0; col < 32; col++) {
        row_text(fgvideo, chars, col, text);
        char *p = strstr(text, word);
        if (p) return (29 - (int)(p - text)) * 32 + col;
    }
    return -1;
}

/* Each letter of the word at cell: its tile's pixels on the screen drawn
   from the assets, in its colour */
static void check_letters(const char *word, int cell)
{
    static uint32_t screen[SF_W * SF_H];
    SfFrame fr;
    sf_frame_from_board(&fr, &board);
    sf_draw(&from_assets, &fr, screen);
    for (int i = 0; word[i]; i++) {
        int c = cell - i * 32, row = c >> 5, col = c & 31;
        int code = board.fgvideo[c] | (board.fgcolor[c] & 0x10) << 4, colour = board.fgcolor[c] & 7;
        if (board.regs[col] || board.fgcolor[c] & 0x80) continue; /* scrolled or flipped: not checked */
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int pen = from_assets.fg[code][y * 8 + x];
                if (!pen) continue;
                int nx = col * 8 + x, ny = row * 8 + y; /* native; the screen's x = 239 - ny, y = nx */
                uint32_t want = sf_colour(board.palette[colour * 8 + pen]);
                if (screen[nx * SF_W + 239 - ny] != want) {
                    CHECK(0, "\"%s\" letter %d: pixel %d,%d is not its tile's", word, i, x, y);
                    y = x = 8;
                }
            }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_extract <starforc.zip|folder> <scratch dir>\n");
        return 2;
    }
    char err[512], a[1100], b[1100];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    mkdir(argv[2], 0755);
    snprintf(a, sizeof a, "%s/a", argv[2]);
    snprintf(b, sizeof b, "%s/b", argv[2]);
    CHECK(!extract_all(&rom, a, err, sizeof err), "extract to %s: %s", a, err);
    CHECK(!extract_all(&rom, b, err, sizeof err), "extract to %s: %s", b, err);
    for (int i = 0; i < FILES; i++) CHECK(same_file(a, b, files[i]), "%s: missing or not byte-identical", files[i]);

    /* read back */
    sf_gfx_from_rom(&from_rom, &rom);
    CHECK(!extract_load_gfx(a, &from_assets), "gfx: cannot load the extracted graphics");
    CHECK(!memcmp(&from_rom, &from_assets, sizeof from_rom), "the graphics differ from the ROM's");
    uint8_t wave[ROM_DAC_SIZE];
    CHECK(!extract_load_wave(a, wave) && !memcmp(wave, rom.dac, sizeof wave), "sound/wave.txt: missing or not 07b.bin's");
    static uint8_t tables[EXTRACT_SPACE];
    CHECK(!extract_load_tables(a, tables, sizeof tables), "tables.txt: cannot load");
    for (unsigned addr = 0; addr < EXTRACT_SPACE; addr++) {
        int want = !extract_is_table(addr) ? 0 : addr < EXTRACT_SUB_BASE ? rom.main[addr] : rom.sub[addr - EXTRACT_SUB_BASE];
        if (tables[addr] != want) {
            CHECK(0, "tables.txt: $%04X is %02X, not %02X", addr, tables[addr], want);
            break;
        }
    }

    /* the font against the attract mode the board draws */
    char chars[256];
    load_font(a, chars);
    board_init(&board, &rom);
    static const struct {
        long frame;
        const char *word;
    } words[] = { { 600, "GAME OVER" }, { 600, "HI-SCORE" }, { 600, "PLAYER1" }, { 1300, "INSERT COIN" } };
    for (size_t w = 0; w < sizeof words / sizeof words[0]; w++) {
        while (board.frame < words[w].frame) board_run_frame(&board);
        int cell = find_word(board.fgvideo, chars, words[w].word);
        CHECK(cell >= 0, "frame %ld: no \"%s\" through the font", words[w].frame, words[w].word);
        if (cell >= 0) check_letters(words[w].word, cell);
    }

    /* no ROM set: nothing written */
    char c[1100], manifest[1200];
    snprintf(c, sizeof c, "%s/none", argv[2]);
    snprintf(manifest, sizeof manifest, "%s/%s", c, EXTRACT_MANIFEST);
    remove(manifest);
    CHECK(extract_all(&none, c, err, sizeof err) == -1, "extraction without a ROM set succeeded");
    struct stat st;
    CHECK(stat(manifest, &st) != 0, "a manifest was written without a ROM set");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("extract: all checks passed");
    return 0;
}
