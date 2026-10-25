#include "assets/rom.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/zip.h"

/* MAME `starforc` (0.289): name, CRC32, size, and where the file goes. */
#define ROM_FILES 19
enum { R_MAIN, R_SUB, R_FGTILES, R_BGTILES1, R_BGTILES2, R_BGTILES3, R_SPRITES, R_DAC };
static const struct {
    const char *mame;
    uint32_t crc;
    unsigned size;
    int region;      /* R_* */
    unsigned offset; /* within the region */
} files[ROM_FILES] = {
    { "3.3p", 0x8ba27691, 0x4000, R_MAIN, 0x0000 },
    { "2.3mn", 0x0fc4d2d6, 0x4000, R_MAIN, 0x4000 },
    { "1.3hj", 0x2735bb22, 0x2000, R_SUB, 0 },
    { "7.2fh", 0xf4803339, 0x1000, R_FGTILES, 0x0000 },
    { "8.3fh", 0x96979684, 0x1000, R_FGTILES, 0x1000 },
    { "9.3fh", 0xeead1d5c, 0x1000, R_FGTILES, 0x2000 },
    { "15.10jk", 0xc3bda12f, 0x2000, R_BGTILES1, 0x0000 },
    { "14.9jk", 0x9e9384fe, 0x2000, R_BGTILES1, 0x2000 },
    { "13.8jk", 0x84603285, 0x2000, R_BGTILES1, 0x4000 },
    { "12.10de", 0xfdd9e38b, 0x2000, R_BGTILES2, 0x0000 },
    { "11.9de", 0x668aea14, 0x2000, R_BGTILES2, 0x2000 },
    { "10.8de", 0xc62a19c1, 0x2000, R_BGTILES2, 0x4000 },
    { "18.10pq", 0x6455c3ad, 0x1000, R_BGTILES3, 0x0000 },
    { "17.9pq", 0x68c60d0f, 0x1000, R_BGTILES3, 0x1000 },
    { "16.8pq", 0xce20b469, 0x1000, R_BGTILES3, 0x2000 },
    { "6.10lm", 0x5468a21d, 0x4000, R_SPRITES, 0x0000 },
    { "5.9lm", 0xf71717f8, 0x4000, R_SPRITES, 0x4000 },
    { "4.8lm", 0xdd9d68a4, 0x4000, R_SPRITES, 0x8000 },
    { "07b.bin", 0x68db8300, 0x20, R_DAC, 0 },
};
#define MAX_FILE_SIZE 0x4000

static const char *const region_names[ROM_REGIONS] = { "maincpu",  "sub",      "fgtiles", "bgtiles1",
                                                       "bgtiles2", "bgtiles3", "sprites", "dac" };

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static uint8_t *region_base(RomSet *rom, int region)
{
    switch (region) {
    case R_MAIN: return rom->main;
    case R_SUB: return rom->sub;
    case R_FGTILES: return rom->fgtiles;
    case R_BGTILES1: return rom->bgtiles1;
    case R_BGTILES2: return rom->bgtiles2;
    case R_BGTILES3: return rom->bgtiles3;
    case R_SPRITES: return rom->sprites;
    default: return rom->dac;
    }
}

static uint8_t *slot(RomSet *rom, int i) { return region_base(rom, files[i].region) + files[i].offset; }

/* Every file of a set file's size in a folder, kept where its CRC matches one. */
static void scan_folder(RomSet *rom, const char *dir, int *found)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    uint8_t buf[MAX_FILE_SIZE];
    while ((e = readdir(d))) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        if (st.st_size != 0x4000 && st.st_size != 0x2000 && st.st_size != 0x1000 && st.st_size != 0x20) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t got = fread(buf, 1, (size_t)st.st_size, f);
        fclose(f);
        if (got != (size_t)st.st_size) continue;
        uint32_t crc = crc32(buf, got);
        for (int i = 0; i < ROM_FILES; i++)
            if (!found[i] && crc == files[i].crc && got == files[i].size) {
                memcpy(slot(rom, i), buf, got);
                found[i] = 1;
            }
    }
    closedir(d);
}

/* The files still missing from a zip, by CRC. Returns -1 if it cannot be read. */
static int scan_zip(RomSet *rom, const char *zip, int *found, char *err, int errlen)
{
    for (int i = 0; i < ROM_FILES; i++) {
        if (found[i]) continue;
        uint8_t *data;
        size_t len;
        int rc = zip_read_crc(zip, files[i].crc, &data, &len, err, errlen);
        if (rc == -1) return -1;
        if (rc == 0) {
            if (len == files[i].size) {
                memcpy(slot(rom, i), data, len);
                found[i] = 1;
            }
            free(data);
        }
    }
    return 0;
}

/* The folder holding path (a zip), or "." */
static void parent_dir(const char *path, char *out, size_t outlen)
{
    snprintf(out, outlen, "%s", path);
    char *slash = strrchr(out, '/');
#ifdef _WIN32
    char *bslash = strrchr(out, '\\');
    if (bslash > slash) slash = bslash;
#endif
    if (slash == out) out[1] = '\0';
    else if (slash) *slash = '\0';
    else snprintf(out, outlen, ".");
}

int rom_load(RomSet *rom, const char *src, char *err, int errlen)
{
    memset(rom, 0, sizeof *rom);
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(err, (size_t)errlen, "%s: not found", src);
        return -1;
    }
    int found[ROM_FILES] = { 0 };
    if (S_ISREG(st.st_mode)) {
        if (scan_zip(rom, src, found, err, errlen)) return -1;
    } else {
        scan_folder(rom, src, found);
    }
    for (int i = 0; i < ROM_FILES; i++)
        if (!found[i]) {
            snprintf(err, (size_t)errlen, "%s: missing %s (CRC %08x); the MAME 'starforc' set is required", src,
                     files[i].mame, (unsigned)files[i].crc);
            memset(rom, 0, sizeof *rom);
            return -1;
        }
    rom->loaded = 1;
    return 0;
}

int rom_byte(const RomSet *rom, unsigned addr)
{
    if (!rom->loaded || addr >= ROM_MAIN_SIZE) return -1;
    return rom->main[addr];
}

const char *rom_region_name(int i) { return i >= 0 && i < ROM_REGIONS ? region_names[i] : NULL; }

const uint8_t *rom_region(const RomSet *rom, int i, unsigned *size)
{
    static const unsigned sizes[ROM_REGIONS] = { ROM_MAIN_SIZE,     ROM_SUB_SIZE,      ROM_FGTILES_SIZE,
                                                 ROM_BGTILES1_SIZE, ROM_BGTILES2_SIZE, ROM_BGTILES3_SIZE,
                                                 ROM_SPRITES_SIZE,  ROM_DAC_SIZE };
    if (i < 0 || i >= ROM_REGIONS || !rom->loaded) return NULL;
    *size = sizes[i];
    return region_base((RomSet *)rom, i);
}

/* want_dir: a folder; else a regular file */
static int exists(const char *path, int want_dir)
{
    struct stat st;
    return stat(path, &st) == 0 && (want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

int rom_locate(const char *argv0, char *out, size_t outlen)
{
    const char *env = getenv("STARFORCE_ROM");
    if (env && *env && (exists(env, 0) || exists(env, 1))) {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    char exe_dir[900] = ".";
    if (argv0) parent_dir(argv0, exe_dir, sizeof exe_dir);
    const char *const dirs[2] = { exe_dir, "." };
    /* MAME's set name (starforce is a clone's) */
    const char *const names[2] = { "starforc.zip", "starforc" };
    for (int d = 0; d < 2; d++)
        for (int n = 0; n < 2; n++) {
            snprintf(out, outlen, "%s/%s", dirs[d], names[n]);
            if (exists(out, n == 1)) return 0;
        }
#ifdef STARFORCE_SOURCE_DIR
    for (int n = 0; n < 2; n++) { /* development builds: the source tree's roms/ */
        snprintf(out, outlen, "%s/roms/%s", STARFORCE_SOURCE_DIR, names[n]);
        if (exists(out, n == 1)) return 0;
    }
#endif
    out[0] = '\0';
    return -1;
}
