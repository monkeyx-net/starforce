/*
 * Zilog Z80 CPU core: every documented and undocumented opcode (the DD/FD
 * index-half registers, SLL, DDCB with a register copy, the ED mirrors),
 * the flags with their undocumented bits 3 and 5 (X, Y; through the
 * internal WZ register for BIT n,(HL), and Q for SCF and CCF), the refresh register, IM 0/1/2,
 * NMI, HALT, and the documented T-state counts without wait states, each
 * memory access at its machine cycle's T-state within the instruction. Written
 * from the Z80's documented behaviour; checked against known instruction
 * results (test z80) and against MAME's traces of the Galaxian, Galaga and Star Force boards
 * (mame_emu). Memory and I/O go through the board's callbacks; an IM 2
 * board's daisy chain answers the acknowledge (irq_ack) and hears RETI.
 */
#ifndef STARFORCE_Z80_H
#define STARFORCE_Z80_H

#include <stdint.h>

#define Z80_CF 0x01
#define Z80_NF 0x02
#define Z80_PF 0x04 /* parity / overflow */
#define Z80_XF 0x08 /* bit 3, undocumented */
#define Z80_HF 0x10
#define Z80_YF 0x20 /* bit 5, undocumented */
#define Z80_ZF 0x40
#define Z80_SF 0x80

typedef struct Z80 {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2; /* the alternate set */
    uint8_t ixh, ixl, iyh, iyl;
    uint16_t sp, pc;
    uint16_t wz;             /* internal: MEMPTR, seen in BIT n,(HL)'s flags */
    uint8_t q, q_set;        /* internal: F if the last instruction set the flags, else 0 (SCF, CCF) */
    uint8_t i, r;            /* r: bit 7 as last loaded, bits 0-6 count opcode fetches */
    uint8_t iff1, iff2, im;
    int halted;
    int ei_delay;            /* the instruction after EI: no maskable interrupt yet */
    int nmi_line;            /* input lines, 1 = asserted; NMI acts on its rising edge */
    int nmi_pending;
    int irq_line;
    uint8_t irq_vector;      /* the byte on the bus when an interrupt is acknowledged */
    int last_interrupt;      /* the last step took an interrupt (traces) */
    int64_t cycles;          /* total T-states executed, to the start of this instruction */
    int t;                   /* during an instruction: the T-state of the machine cycle
                                running (a callback's access is at cycles + t) */
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
    uint8_t (*in)(void *ctx, uint16_t port);   /* optional: NULL reads $FF */
    void (*out)(void *ctx, uint16_t port, uint8_t value); /* optional */
    /* optional: the byte on the bus at an interrupt's acknowledge (else
       irq_vector), and the end of a RETI (a daisy chain's devices watch for it) */
    uint8_t (*irq_ack)(void *ctx);
    void (*reti)(void *ctx);
} Z80;

void z80_reset(Z80 *c);
/* Set the NMI line; a rising edge makes an NMI pending. */
void z80_set_nmi(Z80 *c, int state);
/* Execute one instruction (or take an interrupt, or one HALT cycle);
   returns the T-states used. */
int z80_step(Z80 *c);

#endif
