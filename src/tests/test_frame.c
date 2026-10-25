/*
 * The game in C against the emulated board (label rom):
 *   test_frame <starforc.zip|folder> [frames [dsw1 dsw2 [lives]]]
 * Both from power-on with the tests' player (tests/player.h): an attract
 * cycle, then games to their ends (one player, two, the second coin slot);
 * with `lives`, player one's ships ($8166) kept at 3 in both, so the games
 * go on through the later areas (the ctest runs both).
 * At every frame all memory (main's RAM, video RAM, sprites, palette,
 * registers, bg and radar RAM, the flip, the sub's RAM) and the frame's
 * sound equal the board's. The switches are MAME's defaults unless given
 * (hex). TEST_FRAME_TRACE=n prints both CPUs' registers
 * from frame n on, an instruction a line (the board's), to find a
 * difference.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/sim.h"
#include "tests/player.h"

static RomSet rom;
static Board board;
static Sim sim;
static uint8_t data[GAME_DATA_SIZE];

static const struct {
    const char *name;
    size_t board_off, game_off, len;
} parts[] = {
#define PART(f) { #f, offsetof(Board, f), offsetof(Game, f), sizeof((Board *)0)->f }
    PART(ram), PART(fgvideo), PART(fgcolor), PART(sprites), PART(palette), PART(regs),
    PART(bg3), PART(bg2), PART(bg1), PART(radar), PART(subram),
#undef PART
};

static long trace_from = -1;

static void board_hook(Board *b, int c)
{
    if (trace_from < 0 || (long)b->frame < trace_from) return;
    const Z80 *z = &b->z80[c];
    printf("B%d %lld PC %04X AF %02X%02X BC %02X%02X DE %02X%02X HL %02X%02X SP %04X\n", c,
           (long long)board_total_cycles(b, c), z->pc, z->a, z->f, z->b, z->c, z->d, z->e, z->h, z->l, z->sp);
}

/* the first difference, or 0 */
static int compare(long frame)
{
    for (size_t k = 0; k < sizeof parts / sizeof parts[0]; k++) {
        const uint8_t *x = (const uint8_t *)&board + parts[k].board_off, *y = (const uint8_t *)&sim.g + parts[k].game_off;
        for (size_t i = 0; i < parts[k].len; i++)
            if (x[i] != y[i]) {
                printf("frame %ld: %s +$%03zX: board %02X, C game %02X\n", frame, parts[k].name, i, x[i], y[i]);
                return 1;
            }
    }
    if (board.flip != sim.g.flip) {
        printf("frame %ld: flip: board %d, C game %d\n", frame, board.flip, sim.g.flip);
        return 1;
    }
    if (board.audio_count != sim.audio_count || memcmp(board.audio, sim.audio, (size_t)board.audio_count * 2)) {
        printf("frame %ld: the sound differs (%d and %d samples)\n", frame, board.audio_count, sim.audio_count);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_frame <rom> [frames]\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    long frames = argc > 2 ? atol(argv[2]) : 3 * PLAYER_CYCLE;
    if (getenv("TEST_FRAME_TRACE")) trace_from = atol(getenv("TEST_FRAME_TRACE"));
    memcpy(data, rom.main, ROM_MAIN_SIZE);
    memcpy(data + GAME_SUB_ROM, rom.sub, ROM_SUB_SIZE);
    board_init(&board, &rom);
    board.step_hook = board_hook;
    if (argc > 4) {
        board.dsw1 = (uint8_t)strtoul(argv[3], NULL, 16);
        board.dsw2 = (uint8_t)strtoul(argv[4], NULL, 16);
    }
    int lives = argc > 5 && !strcmp(argv[5], "lives");
    sim_init(&sim, data, board.dsw1, board.dsw2);
    sim_sound(&sim, rom.dac);
    Player player;
    player_init(&player, 1);
    for (long f = 0; f < frames; f++) {
        uint8_t p1, p2, sys;
        player_inputs(&player, f, &p1, &p2, &sys);
        board.p1 = p1, board.p2 = p2, board.system = sys;
        board_run_frame(&board);
        sim_frame(&sim, p1, p2, sys);
        if (sim.g.unported) {
            printf("frame %ld: the C game reached $%04X, not ported\n", f + 1, sim.g.unported);
            return 1;
        }
        if (compare(f + 1)) return 1;
        if (lives && board.ram[0x166] >= 1 && board.ram[0x166] < 3) board.ram[0x166] = sim.g.ram[0x166] = 3;
    }
    printf("frame: the C game equals the board for %ld frames%s\n", frames, lives ? " (ships kept)" : "");
    return 0;
}
