/*
 * The two programs in C: each the whole of its Z80 code (from its reset,
 * its interrupts' entries, the jump tables and the code the board was
 * seen to run, all it reaches), generated line by line from the ROM's code
 * (build/re/tools/z80prog.py, not in the repository) on sim/cpu.h, then
 * read. A program runs from z->pc until its cycles (z->t, counted as the
 * Z80 takes them) reach z->budget; every instruction is a place to go on
 * from, so a scheduler (sim.c) can run the two by turns as the board's
 * does and take their interrupts between instructions. CALL, RET, PUSH and
 * POP go through memory, as the ROM's code expects.
 */
#ifndef STARFORCE_PROGS_H
#define STARFORCE_PROGS_H

#include "sim/cpu.h"

void main_run(Cpu *z); /* main: reset at $0000, IRQ $0038 (IM 1) */
void sub_run(Cpu *z);  /* sub: reset $0000, the PIO's interrupt $00B3, the CTC's $00D0 (IM 2) */

#endif
