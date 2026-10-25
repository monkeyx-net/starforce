/*
 * The tests' player: deterministic inputs for a frame of a run from
 * power-on. It puts in a coin and starts a game every PLAYER_CYCLE frames
 * (one or two players by turns, now and then the second coin slot), and
 * in between moves the ship about and fires in spells drawn from a fixed
 * sequence, so a run goes through the attract mode, games and their ends.
 */
#ifndef STARFORCE_PLAYER_H
#define STARFORCE_PLAYER_H

#include <stdint.h>

#define PLAYER_CYCLE 4000

typedef struct {
    uint32_t seed;
    long hold;        /* frames left of the current move */
    uint8_t p1, p2;   /* the current move */
} Player;

void player_init(Player *p, uint32_t seed);
/* The switches (active high, MAME's ports) for frame f (0 the first). */
void player_inputs(Player *p, long f, uint8_t *p1, uint8_t *p2, uint8_t *system);

#endif
