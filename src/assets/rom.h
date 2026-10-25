/*
 * The user's Star Force ROM set (MAME `starforc`, Tehkan 1984), read from
 * a folder or a zip. Files are matched by CRC-32, so any naming works
 * (MAME's current names, older sets, FBNeo).
 *
 * The regions are MAME's: the two Z80s' programs (main 32 KB, sub 8 KB),
 * the foreground tiles (fgtiles), the three background layers' tiles
 * (bgtiles1-3), the sprites, and the DAC's 32-byte waveform (dac). The
 * set has no colour PROMs and no device ROMs.
 */
#ifndef STARFORCE_ROM_H
#define STARFORCE_ROM_H

#include <stddef.h>
#include <stdint.h>

#define ROM_MAIN_SIZE 0x8000     /* main CPU, $0000-$7FFF */
#define ROM_SUB_SIZE 0x2000      /* sub (sound) CPU, $0000-$1FFF */
#define ROM_FGTILES_SIZE 0x3000  /* 8x8 tiles, three 4 KB bitplanes */
#define ROM_BGTILES1_SIZE 0x6000 /* three 8 KB planes */
#define ROM_BGTILES2_SIZE 0x6000 /* three 8 KB planes */
#define ROM_BGTILES3_SIZE 0x3000 /* three 4 KB planes */
#define ROM_SPRITES_SIZE 0xC000  /* three 16 KB planes */
#define ROM_DAC_SIZE 0x20        /* the DAC's waveform (07b.bin) */

typedef struct {
    uint8_t main[ROM_MAIN_SIZE];
    uint8_t sub[ROM_SUB_SIZE];
    uint8_t fgtiles[ROM_FGTILES_SIZE];
    uint8_t bgtiles1[ROM_BGTILES1_SIZE];
    uint8_t bgtiles2[ROM_BGTILES2_SIZE];
    uint8_t bgtiles3[ROM_BGTILES3_SIZE];
    uint8_t sprites[ROM_SPRITES_SIZE];
    uint8_t dac[ROM_DAC_SIZE];
    int loaded;
} RomSet;

/* Load the set from a folder or a zip. Returns 0, or -1 with a message in
   err. */
int rom_load(RomSet *rom, const char *src, char *err, int errlen);
/* Byte at a main CPU address in its program ROM ($0000-$7FFF); -1 elsewhere. */
int rom_byte(const RomSet *rom, unsigned addr);

/* The regions by MAME's names (maincpu sub fgtiles bgtiles1 bgtiles2
   bgtiles3 sprites dac), in that order: ROM_REGIONS of them. */
#define ROM_REGIONS 8
const char *rom_region_name(int i);
/* Region i's bytes and size; NULL if the set is not loaded. */
const uint8_t *rom_region(const RomSet *rom, int i, unsigned *size);

/* Where the ROM set is: $STARFORCE_ROM, else starforc.zip or a starforc/
   folder next to the program or in the current folder (development
   builds: also in the source tree's roms/). Returns 0 if found. */
int rom_locate(const char *argv0, char *out, size_t outlen);

#endif
