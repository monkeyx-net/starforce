/*
 * Controls, sampled once per frame. Star Force has two players, each with
 * an 8-way joystick and a fire button (MAME's P1 and P2), and two coin
 * slots. Pads go through SDL_GameController (SDL_GAMECONTROLLERCONFIG /
 * gamecontrollerdb.txt): with two pads, the first is player one and the
 * second player two; with one, it and the keyboard drive both (the
 * players take turns).
 *
 *              pad                          keyboard
 *   move       left stick, D-pad            arrows (player one)
 *   fire       A or B                       Space or Ctrl
 *   coin       Select                       5 (6: the second slot)
 *   start      Start (1P), L1+Start (2P)    1 or Enter / 2
 *   graphics   Select+L1                    F3 (F4 glow, F5 scanlines)
 *   operator menu Select+R1               F2 (up/down, left/right in it)
 *   quit       hold Select+Start 1 s        Esc
 */
#ifndef STARFORCE_INPUT_H
#define STARFORCE_INPUT_H

#include <stdbool.h>

/* Frames Select+Start must be held to quit (1 s). */
#define QUIT_HOLD_FRAMES 60
#define MAX_PADS 2

typedef struct {
    int move_x;          /* -1 left, 0, 1 right */
    int move_y;          /* -1 up, 0, 1 down */
    bool fire;
} PlayerInput;

typedef struct {
    PlayerInput p[2];    /* players one and two */
    int move_x, move_y;  /* any player's stick (the operator menu) */
    bool coin;           /* coin A */
    bool coin2;          /* coin B */
    bool start;          /* 1-player start */
    bool start2;         /* 2-player start */
    bool quit;           /* Select+Start held QUIT_HOLD_FRAMES, Esc, or window close */
    bool gfx_next;       /* Select+L1 or F3, edge-triggered: next graphics preset */
    bool gfx_glow;       /* F4, edge-triggered */
    bool gfx_scanlines;  /* F5, edge-triggered */
    bool menu;           /* Select+R1 or F2, edge-triggered: the operator menu */
    int pads;            /* game controllers connected */
} InputFrame;

void input_init(void);
/* Pump SDL events and build this frame's input. */
void input_poll(InputFrame *out);
void input_shutdown(void);

#endif
