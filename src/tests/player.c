#include "tests/player.h"

#include "emu/board.h"

static uint32_t next(Player *p)
{
    p->seed = p->seed * 1103515245u + 12345u;
    return p->seed >> 16;
}

/* a stick's move: no two opposite directions */
static uint8_t move(Player *p)
{
    uint8_t v = (uint8_t)(next(p) & 0x1F);
    if ((v & (IN_LEFT | IN_RIGHT)) == (IN_LEFT | IN_RIGHT)) v &= (uint8_t)~IN_RIGHT;
    if ((v & (IN_UP | IN_DOWN)) == (IN_UP | IN_DOWN)) v &= (uint8_t)~IN_DOWN;
    return v;
}

void player_init(Player *p, uint32_t seed)
{
    p->seed = seed;
    p->hold = 0;
    p->p1 = p->p2 = 0;
}

void player_inputs(Player *p, long f, uint8_t *p1, uint8_t *p2, uint8_t *system)
{
    long cycle = f / PLAYER_CYCLE, k = f % PLAYER_CYCLE;
    int two = cycle % 3 == 1;
    uint8_t sys = 0;
    /* coins pulse for 2 frames, as MAME's */
    if (k >= 600 && k < 602) sys |= cycle % 4 == 3 ? SYS_COIN2 : SYS_COIN1;
    if (two && k >= 620 && k < 622) sys |= SYS_COIN1;
    if (k >= 700 && k < 710) sys |= two ? SYS_START2 : SYS_START1;
    if (--p->hold <= 0) {
        p->hold = 5 + (long)(next(p) % 40);
        p->p1 = move(p);
        p->p2 = move(p);
    }
    *p1 = p->p1;
    *p2 = p->p2;
    *system = sys;
}
