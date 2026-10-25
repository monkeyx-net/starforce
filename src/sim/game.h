/*
 * The C game's state: the board's memory as the ROMs lay it out, main's
 * (RAM $8000-$8FFF, the fg layer's codes and colours, the sprites, the
 * palette, the registers, the bg layers' RAM, the radar RAM; the flip, the
 * switches) and the sub's (RAM $4000-$43FF). The two programs reach it at
 * the ROMs' addresses (game_rd / game_wr for main, game_sub_rd /
 * game_sub_wr for the sub, mapped as the board's), so the whole state
 * compares byte for byte with the board's.
 *
 * `data` is what the programs read of their ROMs, at its ROM addresses:
 * main's at $0000-$7FFF, the sub's at GAME_SUB_ROM + its address
 * (assets/extract.h); the game fills it from assets/, the tests from the
 * ROM set.
 */
#ifndef STARFORCE_GAME_H
#define STARFORCE_GAME_H

#include <stdint.h>

#define GAME_DATA_SIZE 0xA000
#define GAME_SUB_ROM 0x8000 /* the sub's ROM in data */
enum { GAME_MAIN, GAME_SUB };

typedef struct Game {
    uint8_t ram[0x1000];       /* $8000 */
    uint8_t fgvideo[0x400];    /* $9000 */
    uint8_t fgcolor[0x400];    /* $9400 */
    uint8_t sprites[0x80];     /* $9800 */
    uint8_t palette[0x200];    /* $9C00 */
    uint8_t regs[0x40];        /* $9E00 */
    uint8_t bg3[0x800];        /* $A000 */
    uint8_t bg2[0x800];        /* $A800 */
    uint8_t bg1[0x800];        /* $B000 */
    uint8_t radar[0x400];      /* $B800 */
    uint8_t flip;              /* $D000 bit 0 */
    uint8_t p1, p2, system, dsw1, dsw2; /* the switches (active high) */
    uint8_t subram[0x400];     /* the sub's $4000 */
    const uint8_t *data;       /* GAME_DATA_SIZE bytes: the ROMs */
    uint16_t unported;         /* the ROM address the C game last reached unported, or 0 */
    /* the scheduler's (sim/sim.c): main's writes with an effect beyond the
       memory ($D002, $D004), the sub's (its sound chips), its I/O (the PIO
       and the CTC), its RETIs */
    void (*bus_write)(void *bus, int cpu, uint16_t addr, uint8_t value);
    uint8_t (*io_read)(void *bus, uint8_t port);
    void (*io_write)(void *bus, uint8_t port, uint8_t value);
    void (*reti)(void *bus);
    void *bus;
    /* optional, for tests: called before each write of either CPU */
    void (*on_write)(void *user, int cpu, uint16_t addr, uint8_t value);
    void *user;
} Game;

/* A byte as main reads it ($8000 up; 0 where nothing answers). */
uint8_t game_rd(const Game *g, uint16_t addr);
/* A byte as main writes it. */
void game_wr(Game *g, uint16_t addr, uint8_t value);
/* A byte as the sub reads it ($2000 up). */
uint8_t game_sub_rd(const Game *g, uint16_t addr);
/* A byte as the sub writes it. */
void game_sub_wr(Game *g, uint16_t addr, uint8_t value);
/* A byte of a ROM (data: main $0000, sub GAME_SUB_ROM +). */
uint8_t game_data(const Game *g, unsigned addr);

/* The C game reached ROM code not ported: remember where (the caller must
   leave the state unspecified). */
void game_unported(Game *g, uint16_t addr);

#endif
