/*
 * The Star Force board (Tehkan, 1984): the main Z80 at 4 MHz running the
 * game, the sub Z80 at 2 MHz running the sound with a Z80 PIO and a Z80 CTC
 * on its daisy chain, the video RAM and registers, three SN76489As and a
 * DAC.
 *
 * Main: ROM $0000-$7FFF, RAM $8000-$8FFF, the fg layer's codes $9000 and
 * colours $9400, the sprites $9800-$987F, the palette $9C00-$9DFF, the
 * scroll registers $9E00-$9E3F, the bg layers' RAM $A000 (bg3), $A800
 * (bg2), $B000 (bg1), the radar RAM $B800-$BBFF; reads $D000 P1, $D001 P2,
 * $D002 SYSTEM, $D004 DSW1, $D005 DSW2 (active high); writes $D000 the
 * flip, $D002 the IRQ's acknowledge, $D004 the sound latch. Unmapped reads
 * give 0. Main's IRQ (IM 1) comes at the vertical blank (line 240, where
 * frames start), held until $D002 is written.
 *
 * Sub: ROM $0000-$1FFF, RAM $4000-$43FF; writes $8000, $9000, $A000 the
 * three SN76489As, $D000 the DAC's volume, $E000 (unknown, E2); I/O $00-$03
 * the PIO (bit 0 control, bit 1 port B), $08-$0B the CTC's channels. The
 * PIO's port A reads the sound latch; each latch write strobes it (bar the
 * first since power-on), its interrupt pending from power-on. The CTC
 * counts the sub's clock; channel 0's zero count clocks channel 1 (the
 * sound driver's tick), channel 2's steps the DAC through its waveform.
 *
 * Time is kept as MAME keeps it, in attoseconds, and the CPUs run in MAME's
 * slices, cut short by timers (the screen's, the CTC's, the sound update):
 * main, then sub, each to the slice's end, a CPU that stops short bringing
 * the end back to its time. A write that changes another CPU's lines takes
 * effect once the others have caught up (MAME's synchronize), and stops
 * the writing CPU's slice. A Z80 stops between machine cycles when its
 * cycles run out, the rest of the instruction left for its next slice
 * (plan section 2).
 */
#ifndef STARFORCE_BOARD_H
#define STARFORCE_BOARD_H

#include <setjmp.h>
#include <stdint.h>

#include "assets/rom.h"
#include "emu/sound.h"
#include "emu/z80.h"

#define BOARD_CPUS 2 /* main, sub: MAME's order */
enum { CPU_MAIN, CPU_SUB };

#define BOARD_LINES 264
#define BOARD_VBLANK_LINE 240 /* frames start here */
#define BOARD_MAIN_HZ 4000000
#define BOARD_SUB_HZ 2000000

/* Inputs, active high, as MAME's ports */
#define IN_RIGHT 0x01
#define IN_LEFT 0x02
#define IN_UP 0x04
#define IN_DOWN 0x08
#define IN_FIRE 0x10
#define SYS_COIN1 0x01
#define SYS_COIN2 0x02
#define SYS_START1 0x04
#define SYS_START2 0x08
#define DSW1_DEFAULT 0xC0 /* 1C/1C both slots, 3 lives, upright, demo sounds on */
#define DSW2_DEFAULT 0x00 /* 50K 200K 500K, easiest */

/* MAME's attotime: seconds and attoseconds */
typedef struct {
    int64_t sec, as;
} Atime;

typedef struct Board Board;

typedef struct {
    Atime expire, period; /* period 0: once */
    int active;
    int64_t seq;          /* set in this order (equal times fire in it) */
    void (*fire)(Board *b, int param);
    int param;
} BoardTimer;

#define BOARD_TIMERS 16

/* A CTC channel */
typedef struct {
    uint8_t control;  /* the last control word */
    uint8_t tc;       /* the time constant (0: 256) */
    int want_tc;      /* the next write is a time constant */
    int running;      /* counting (timer mode: its timer set) */
    int down;         /* counter mode: counts left */
    int ip, ius;      /* interrupt pending, under service */
} CtcChannel;

/* A sound chip write, timed in the frame (E2 makes the samples) */
typedef struct {
    int64_t at;      /* attoseconds since the frame's start */
    uint8_t chip;    /* 0-2 the SN76489As, 3 the DAC's volume, 4 $E000, 5 a DAC step */
    uint8_t value;
} SoundEvent;

#define BOARD_SOUND_EVENTS 4096
#define BOARD_AUDIO_MAX 1024 /* a frame's samples: 811 or 812 at 48 kHz */

struct Board {
    Z80 z80[BOARD_CPUS];
    const RomSet *rom;
    Atime local[BOARD_CPUS];   /* each CPU's own time */
    int64_t total[BOARD_CPUS]; /* cycles run */
    Atime base;                /* the scheduler's time: the last slice's end */
    Atime target;              /* the slice's end */
    int executing;             /* the CPU running, or -1 */
    int64_t run_cycles, icount; /* its cycles to run, and left */
    int abort_slice;           /* the running CPU stops after its instruction */
    /* MAME stops a Z80 between machine cycles once its slice's cycles are
       spent: an access that would come later is left for its next slice.
       The instruction is then run again from its start, its accesses so far
       replayed (part[]). */
    struct {
        int active;            /* stopped inside an instruction */
        Z80 start;             /* the CPU when the instruction began */
        uint8_t value[16];     /* what its accesses so far read */
        int done;              /* accesses made */
        int t;                 /* the instruction's cycles run */
        int irq;               /* the IRQ line held meanwhile, or -1 */
    } part[BOARD_CPUS];
    int access, replay;        /* the running Z80's accesses in this instruction, and to replay */
    int t0;                    /* its instruction's cycles run in earlier slices */
    int abort_t;               /* the T-state its slice was aborted at, or -1 */
    int64_t icount_start;      /* its cycles left when the instruction began */
    jmp_buf stop;
    BoardTimer timers[BOARD_TIMERS];
    int64_t timer_seq;
    int in_callback;           /* a timer's callback runs: its time is now */
    Atime callback_time;

    /* main's memory */
    uint8_t ram[0x1000];       /* $8000 */
    uint8_t fgvideo[0x400];    /* $9000 */
    uint8_t fgcolor[0x400];    /* $9400 */
    uint8_t sprites[0x80];     /* $9800 */
    uint8_t palette[0x200];    /* $9C00 */
    uint8_t regs[0x40];        /* $9E00: fg column scroll, bg scroll, stripes */
    uint8_t bg3[0x800];        /* $A000 */
    uint8_t bg2[0x800];        /* $A800 */
    uint8_t bg1[0x800];        /* $B000 */
    uint8_t radar[0x400];      /* $B800 */
    uint8_t flip;              /* $D000 bit 0 */
    uint8_t p1, p2, system, dsw1, dsw2;
    int irq_main;              /* the vertical blank's IRQ, held until $D002 */

    /* sub's */
    uint8_t subram[0x400];     /* $4000 */
    uint8_t latch;             /* the sound latch */
    int latch_written;         /* written since power-on */
    struct {
        uint8_t vector, mode, icw, mask;
        int want_mask, ie, ip, ius;
        uint8_t out_b;         /* port B's output */
        uint8_t ctrl_b[3];     /* port B's control words (vector, mode, interrupts) */
    } pio;
    struct {
        CtcChannel ch[4];
        uint8_t vector;
    } ctc;
    int irq_sub;               /* the daisy chain's interrupt line */

    SoundEvent sound[BOARD_SOUND_EVENTS];
    int sound_count;
    Sound chips;
    int16_t audio[BOARD_AUDIO_MAX]; /* the last frame's samples, made at its end */
    int audio_count;

    int64_t frame;             /* frames run */
    Atime frame_start;         /* the frame's vertical blank */
    int frame_done;
    /* optional: called before each instruction of each CPU (traces) */
    void (*step_hook)(Board *b, int cpu);
    /* optional: called on every CPU write (memory and I/O, I/O ports + $10000), and every read */
    void (*write_hook)(Board *b, int cpu, uint32_t addr, uint8_t value);
    void (*read_hook)(Board *b, int cpu, uint32_t addr, uint8_t value);
    void *user;
};

/* Power on. */
void board_init(Board *b, const RomSet *rom);
/* Run one frame: to the next vertical blank. */
void board_run_frame(Board *b);
/* A main CPU memory read as it sees it (no side effects). */
uint8_t board_peek(Board *b, uint16_t addr);
/* The cycles a CPU has run (MAME's totalcycles), during its instruction too. */
int64_t board_total_cycles(const Board *b, int cpu);

#endif
