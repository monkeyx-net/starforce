#include "sim/game.h"

uint8_t game_rd(const Game *g, uint16_t a)
{
    if (a < 0x8000) return g->data ? g->data[a] : 0;
    if (a < 0x9000) return g->ram[a & 0xFFF];
    if (a < 0x9400) return g->fgvideo[a & 0x3FF];
    if (a < 0x9800) return g->fgcolor[a & 0x3FF];
    if (a < 0x9880) return g->sprites[a & 0x7F];
    if (a >= 0x9C00 && a < 0x9E00) return g->palette[a & 0x1FF];
    if (a >= 0x9E00 && a < 0x9E40) return g->regs[a & 0x3F];
    if (a >= 0xA000 && a < 0xA800) return g->bg3[a & 0x7FF];
    if (a >= 0xA800 && a < 0xB000) return g->bg2[a & 0x7FF];
    if (a >= 0xB000 && a < 0xB800) return g->bg1[a & 0x7FF];
    if (a >= 0xB800 && a < 0xBC00) return g->radar[a & 0x3FF];
    switch (a) {
    case 0xD000: return g->p1;
    case 0xD001: return g->p2;
    case 0xD002: return g->system;
    case 0xD004: return g->dsw1;
    case 0xD005: return g->dsw2;
    default: return 0;
    }
}

void game_wr(Game *g, uint16_t a, uint8_t v)
{
    if (g->on_write) g->on_write(g->user, GAME_MAIN, a, v);
    if (a < 0x8000) return;
    if (a < 0x9000) g->ram[a & 0xFFF] = v;
    else if (a < 0x9400) g->fgvideo[a & 0x3FF] = v;
    else if (a < 0x9800) g->fgcolor[a & 0x3FF] = v;
    else if (a < 0x9880) g->sprites[a & 0x7F] = v;
    else if (a >= 0x9C00 && a < 0x9E00) g->palette[a & 0x1FF] = v;
    else if (a >= 0x9E00 && a < 0x9E40) g->regs[a & 0x3F] = v;
    else if (a >= 0xA000 && a < 0xA800) g->bg3[a & 0x7FF] = v;
    else if (a >= 0xA800 && a < 0xB000) g->bg2[a & 0x7FF] = v;
    else if (a >= 0xB000 && a < 0xB800) g->bg1[a & 0x7FF] = v;
    else if (a >= 0xB800 && a < 0xBC00) g->radar[a & 0x3FF] = v;
    else if (a == 0xD000) g->flip = v & 1;
    if ((a == 0xD002 || a == 0xD004) && g->bus_write) g->bus_write(g->bus, GAME_MAIN, a, v);
}

uint8_t game_sub_rd(const Game *g, uint16_t a)
{
    if (a < 0x2000) return g->data ? g->data[GAME_SUB_ROM + a] : 0;
    if (a >= 0x4000 && a < 0x4400) return g->subram[a & 0x3FF];
    return 0;
}

void game_sub_wr(Game *g, uint16_t a, uint8_t v)
{
    if (g->on_write) g->on_write(g->user, GAME_SUB, a, v);
    if (a >= 0x4000 && a < 0x4400) g->subram[a & 0x3FF] = v;
    else if ((a == 0x8000 || a == 0x9000 || a == 0xA000 || a == 0xD000 || a == 0xE000) && g->bus_write)
        g->bus_write(g->bus, GAME_SUB, a, v);
}

uint8_t game_data(const Game *g, unsigned addr) { return addr < GAME_DATA_SIZE && g->data ? g->data[addr] : 0; }

void game_unported(Game *g, uint16_t addr)
{
    if (!g->unported) g->unported = addr;
}
