/*
 * The ROM set loader against the user's starforc set (label rom):
 *   test_rom <starforc.zip or folder> <scratch dir>
 * Loads it as given, then from a folder of the same ROMs under other names
 * (matching is by CRC) with unrelated files, then checks an empty folder
 * fails with a message.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/rom.h"

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

static RomSet a, b;

static void write_rom(const char *dir, const char *name, const uint8_t *data, size_t len)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(data, 1, len, f) == len, "cannot write %s", path);
    if (f) fclose(f);
}

/* The set under made-up names in dir, the planes in another order. */
static void write_renamed(const char *dir)
{
    static const struct {
        const char *name;
        const uint8_t *base;
        size_t offset, len;
    } parts[] = {
        { "prog_b.bin", a.main, 0x4000, 0x4000 },  { "prog_a.bin", a.main, 0x0000, 0x4000 },
        { "sound.bin", a.sub, 0, 0x2000 },         { "fg_2.rom", a.fgtiles, 0x2000, 0x1000 },
        { "fg_0.rom", a.fgtiles, 0x0000, 0x1000 }, { "fg_1.rom", a.fgtiles, 0x1000, 0x1000 },
        { "bga_0.rom", a.bgtiles1, 0x0000, 0x2000 }, { "bga_2.rom", a.bgtiles1, 0x4000, 0x2000 },
        { "bga_1.rom", a.bgtiles1, 0x2000, 0x2000 }, { "bgb_1.rom", a.bgtiles2, 0x2000, 0x2000 },
        { "bgb_0.rom", a.bgtiles2, 0x0000, 0x2000 }, { "bgb_2.rom", a.bgtiles2, 0x4000, 0x2000 },
        { "bgc_2.rom", a.bgtiles3, 0x2000, 0x1000 }, { "bgc_0.rom", a.bgtiles3, 0x0000, 0x1000 },
        { "bgc_1.rom", a.bgtiles3, 0x1000, 0x1000 }, { "obj_2.rom", a.sprites, 0x8000, 0x4000 },
        { "obj_1.rom", a.sprites, 0x4000, 0x4000 }, { "obj_0.rom", a.sprites, 0x0000, 0x4000 },
        { "wave.prom", a.dac, 0, 0x20 },
    };
    mkdir(dir, 0755);
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++)
        write_rom(dir, parts[i].name, parts[i].base + parts[i].offset, parts[i].len);
    static uint8_t junk[0x4000];
    memset(junk, 0x5A, sizeof junk);
    write_rom(dir, "unrelated.bin", junk, sizeof junk);
    write_rom(dir, "unrelated.prom", junk, 0x20);
}

static int same_set(const RomSet *x, const RomSet *y)
{
    return !memcmp(x->main, y->main, sizeof x->main) && !memcmp(x->sub, y->sub, sizeof x->sub) &&
           !memcmp(x->fgtiles, y->fgtiles, sizeof x->fgtiles) &&
           !memcmp(x->bgtiles1, y->bgtiles1, sizeof x->bgtiles1) &&
           !memcmp(x->bgtiles2, y->bgtiles2, sizeof x->bgtiles2) &&
           !memcmp(x->bgtiles3, y->bgtiles3, sizeof x->bgtiles3) &&
           !memcmp(x->sprites, y->sprites, sizeof x->sprites) && !memcmp(x->dac, y->dac, sizeof x->dac);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_rom <starforc.zip|folder> <scratch dir>\n");
        return 2;
    }
    char err[512];
    CHECK(!rom_load(&a, argv[1], err, sizeof err), "load %s: %s", argv[1], err);
    CHECK(rom_byte(&a, 0x0000) == a.main[0] && rom_byte(&a, 0x7FFF) == a.main[0x7FFF], "program ROM mapping");
    CHECK(rom_byte(&a, 0x8000) == -1 && rom_byte(&a, 0xC000) == -1, "RAM addresses read as ROM");
    unsigned size;
    CHECK(rom_region(&a, 0, &size) == a.main && size == 0x8000, "region maincpu");
    CHECK(rom_region(&a, 6, &size) == a.sprites && size == 0xC000, "region sprites");
    CHECK(rom_region(&a, 7, &size) == a.dac && size == 0x20, "region dac");
    CHECK(!strcmp(rom_region_name(3), "bgtiles1") && !rom_region_name(ROM_REGIONS), "region names");

    /* the same ROMs under other names in a folder, with unrelated files */
    mkdir(argv[2], 0755);
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/renamed", argv[2]);
    write_renamed(dir);
    CHECK(!rom_load(&b, dir, err, sizeof err), "load renamed folder: %s", err);
    CHECK(same_set(&a, &b), "renamed folder loaded different ROMs");

    snprintf(dir, sizeof dir, "%s/empty", argv[2]);
    mkdir(dir, 0755);
    err[0] = '\0';
    CHECK(rom_load(&b, dir, err, sizeof err) == -1 && strstr(err, "missing 3.3p"),
          "empty folder: rc/err '%s'", err);
    CHECK(!b.loaded && rom_byte(&b, 0) == -1 && !rom_region(&b, 0, &size), "failed load left the set usable");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("rom: all checks passed");
    return 0;
}
