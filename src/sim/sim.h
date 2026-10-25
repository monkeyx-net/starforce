/*
 * The C game: the two programs (sim/progs.h) over the shared state, run by
 * a scheduler like the board's (MAME's), its time in attoseconds as MAME
 * keeps it: in slices of 1/60 s or to the next timer, main then the sub,
 * each on to the slice's end by its cycles (a Z80 stopped between machine
 * cycles there, as MAME's is; one that ends short brings the slice's end
 * back to it); the board's timers (the vertical blank, line 0, the
 * vertical blank's end, the sound update, the CTC's zero counts); the sub's
 * PIO and CTC on its daisy chain; writes that change a CPU's interrupt
 * line, and the sound latch, end the writer's slice there and take effect
 * once the other has caught up (emu/board.h has the board).
 */
#ifndef STARFORCE_SIM_H
#define STARFORCE_SIM_H

#include "emu/sound.h"
#include "sim/cpu.h"
#include "sim/game.h"

#define SIM_AUDIO_MAX 1024 /* a frame's samples: 811 or 812 at 48 kHz */
#define SIM_SOUND_EVENTS 4096

enum { SIM_MAIN, SIM_SUB, SIM_CPUS };

typedef struct {
    int64_t sec, as;
} SimTime;

typedef struct {
    SimTime at;
    int64_t period;  /* attoseconds, or 0: once */
    int64_t seq;     /* set in this order (equal times fire in it) */
    int what, param;
    int active;
} SimTimer;

#define SIM_TIMERS 16

typedef struct {
    uint8_t control, tc;
    int want_tc, running, down, ip, ius;
} SimCtc;

typedef struct Sim {
    Game g;
    Cpu cpu[SIM_CPUS];
    SimTime local[SIM_CPUS]; /* each CPU's time */
    int64_t done[SIM_CPUS];  /* its cycles counted into it (a stop inside an instruction's too) */
    SimTime base;            /* the scheduler's time: the last slice's end */
    SimTime target;          /* the slice's end */
    int running;             /* the CPU running, or -1 */
    int aborted;             /* a timer set while it ran ended its slice */
    SimTimer timers[SIM_TIMERS];
    int64_t seq;
    int in_event;            /* a timer's event: its time is now */
    SimTime event_time;
    int frame_done;
    long frame;
    int irq_main, irq_sub;   /* the CPUs' interrupt lines as last set */
    uint8_t latch;           /* the sound latch */
    int latch_written;       /* written since power-on */
    struct {
        uint8_t vector;
        int want_mask, ie, ip, ius;
    } pio;                   /* the PIO's port A (port B has no part) */
    SimCtc ctc[4];
    uint8_t ctc_vector;
    /* the sound: the frame's writes timed from its start, rendered at its end */
    struct {
        int64_t at;
        uint8_t chip, value;
    } sound_events[SIM_SOUND_EVENTS];
    int sound_event_count;
    Sound chips;
    int16_t audio[SIM_AUDIO_MAX];
    int audio_count;
    SimTime frame_start;     /* the frame's vertical blank */
} Sim;

/* Power on; `data` the programs' bytes (GAME_DATA_SIZE). */
void sim_init(Sim *s, const uint8_t *data, uint8_t dsw1, uint8_t dsw2);
/* The sound on: the DAC's waveform (07b.bin's 32 bytes) */
void sim_sound(Sim *s, const uint8_t *wave);
/* One frame, to the next vertical blank, with the switches (active high) */
void sim_frame(Sim *s, uint8_t p1, uint8_t p2, uint8_t system);

#endif
