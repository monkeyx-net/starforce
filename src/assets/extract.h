/*
 * Extractor: reads the user's ROM set once and writes what the game draws
 * and plays under <out>/ (PROGRESS_STARFORCE_PORT.md, decision 1). After
 * that the game in C needs only <out>/, never the ROM set. Nothing here is
 * compiled in: the data comes from the player's set.
 *
 *   gfx/fg.txt          the 512 fg tiles (7.2fh, 8.3fh, 9.3fh): "tile NNN"
 *                       then 8 rows of 8 pens (0-7), in the board's own
 *                       orientation (gfx/starforce.h)
 *   gfx/bg1.txt         the 256 bg1 tiles (15.10jk, 14.9jk, 13.8jk): "tile
 *                       NNN" then 16 rows of 16 pens
 *   gfx/bg2.txt         the 256 bg2 tiles (12.10de, 11.9de, 10.8de)
 *   gfx/bg3.txt         the 128 bg3 tiles (18.10pq, 17.9pq, 16.8pq)
 *   gfx/sprites.txt     the 512 16x16 sprites (6.10lm, 5.9lm, 4.8lm), the
 *                       32x32 ones four of them
 *   gfx/font.txt        the game's characters: "NN c", NN the fg tile
 *   gfx/<name>.png      previews, as the player sees them (not read by the
 *                       game): the tile sets in grey, and the attract
 *                       mode's screens from the emulated board
 *   sound/wave.txt      the DAC's waveform PROM (07b.bin, 32 bytes)
 *   tables.txt          the program ROMs the game in C reads: "# what"
 *                       then "ADDR: bytes" lines, 16 bytes each, for each
 *                       range listed in extract.c (addresses only there;
 *                       main's program, then sub's at +$8000)
 *   manifest.txt        written last: the folder is complete
 */
#ifndef STARFORCE_EXTRACT_H
#define STARFORCE_EXTRACT_H

#include <stdint.h>

#include "assets/rom.h"
#include "gfx/starforce.h"

#define EXTRACT_MANIFEST "manifest.txt"
#define EXTRACT_VERSION 1

/* The programs' address space in tables.txt: main, then sub */
#define EXTRACT_SUB_BASE 0x8000
#define EXTRACT_SPACE 0xA000

/* Write everything to out_dir (created if needed). Returns 0, or -1 with a
   message in err; without manifest.txt the folder counts as missing. */
int extract_all(const RomSet *rom, const char *out_dir, char *err, int errlen);

/* 1 if the byte at addr (main, or sub from its base) is in a table the
   extractor writes. */
int extract_is_table(unsigned addr);
/* Read tables.txt under dir into data (indexed as above, the rest 0).
   Returns 0, or -1 if missing or incomplete. */
int extract_load_tables(const char *dir, uint8_t *data, unsigned size);
/* Read sound/wave.txt under dir: 32 bytes. 0 or -1. */
int extract_load_wave(const char *dir, uint8_t *wave);
/* Read the graphics under dir/gfx. 0, or -1 if missing or bad. */
int extract_load_gfx(const char *dir, SfGfx *g);

#endif
