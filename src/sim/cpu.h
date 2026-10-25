/*
 * A Z80's registers and flags for its program ported line by line
 * (sim/progs.h), where a ROM's code carries values in registers and flags
 * from one block to the next (a register left over from an earlier block
 * feeding a division, a carry feeding a rotate). F is the whole byte, as
 * an interrupt's PUSH AF puts it in memory: each operation sets it as the
 * Z80 does (the board's, emu/z80.c), its undocumented bits 3 and 5 too,
 * which SCF and CCF take from Q (F, when the instruction before set it) and
 * BIT n,(HL) from the internal WZ, which the instructions that set it set
 * here too.
 *
 * Like the board's Z80 (MAME's), a program stops between machine cycles
 * when its slice ends inside an instruction: at a fetch after its first
 * (a prefix's opcode, an operand: cpu_fetch, before it does anything, as
 * the fetches come first), or at a memory access: an instruction that
 * would cross its budget keeps its registers at its start (cpu_part); an
 * access at or past the budget (each instruction's accesses have their
 * machine cycles' offsets) then stops it there (longjmp to the scheduler),
 * and the instruction is run again in its next slice, its accesses so far
 * replayed: the reads with the values they had, the writes not again.
 */
#ifndef STARFORCE_CPU_H
#define STARFORCE_CPU_H

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "sim/game.h"

typedef struct Cpu {
    /* the registers (cpu_part keeps them, from a to ei_delay) */
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t ix, iy;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2; /* AF', BC', DE', HL' */
    uint16_t sp;                /* the stack pointer: PUSH and POP go through memory */
    uint16_t wz;                /* the internal WZ (MEMPTR) */
    uint8_t r, r7;              /* the refresh register: its count of opcode fetches, and bit 7 as loaded */
    uint8_t iff, iff2;          /* interrupts enabled (EI / DI; RETN restores from IFF2) */
    uint8_t i;                  /* the interrupt vector's page (IM 2) */
    uint8_t ei_delay;           /* an EI with the IRQ up: the next instruction, then the IRQ */
    int64_t q_t;                /* where the last instruction that set F ended (Q: F if it is the one before) */
    uint8_t regs_end;
    /* a program run resumably: */
    Game *g;
    uint8_t id;                 /* GAME_MAIN or GAME_SUB: whose memory map */
    uint16_t pc;                /* where it goes on */
    uint8_t halted;             /* stopped at a HALT until an interrupt */
    uint8_t irq_line, nmi;      /* its IRQ line (level), an NMI to take */
    int64_t t, budget;          /* the Z80 cycles run, and where it stops (between instructions) */
    int64_t limit;              /* where its accesses stop (the slice's end) */
    uint16_t stop_pc, stop_sp;  /* it stops when a RET (or JP (HL)) brings it there (the tests) */
    /* an instruction across its budget (cpu_part): */
    uint8_t stopped;            /* stopped inside it (at part_pc): to finish before an interrupt */
    uint8_t in_int;             /* it is an interrupt's acceptance (sim.c): 1 the NMI, 2 the IRQ */
    uint8_t partial;            /* its accesses checked */
    uint8_t acc;                /* its accesses so far */
    uint8_t rep_n, rep_now;     /* accesses made before it stopped (to replay), and in this run */
    uint8_t rep[8];             /* their reads' values */
    const uint8_t *offs;        /* its accesses' offsets in cycles */
    uint16_t part_pc;           /* where it starts */
    int64_t t_start, t_stop;    /* its start, and where it stopped */
    int64_t t_access;           /* the machine cycle's start of the write under way (cycles) */
    jmp_buf *jb;                /* the scheduler's, for the stop */
    uint8_t saved[64];          /* the registers at its start */
} Cpu;

_Static_assert(offsetof(Cpu, regs_end) <= 64, "Cpu's registers fit saved[]");

#define HL(z) ((uint16_t)((z)->h << 8 | (z)->l))
#define DE(z) ((uint16_t)((z)->d << 8 | (z)->e))
#define BC(z) ((uint16_t)((z)->b << 8 | (z)->c))
#define SET_HL(z, v) cpu_pair(&(z)->h, &(z)->l, (unsigned)(v))
#define SET_DE(z, v) cpu_pair(&(z)->d, &(z)->e, (unsigned)(v))
#define SET_BC(z, v) cpu_pair(&(z)->b, &(z)->c, (unsigned)(v))
static inline void cpu_pair(uint8_t *hi, uint8_t *lo, unsigned v)
{
    *hi = (uint8_t)(v >> 8);
    *lo = (uint8_t)v;
}
static inline void cpu_ex_de_hl(Cpu *z)
{
    uint8_t d = z->d, e = z->e;
    z->d = z->h, z->e = z->l;
    z->h = d, z->l = e;
}

static inline void cpu_ex_af(Cpu *z)
{
    uint8_t t;
#define SWAP(x, y) (t = (x), (x) = (y), (y) = t)
    SWAP(z->a, z->a2);
    SWAP(z->f, z->f2);
}
static inline void cpu_exx(Cpu *z)
{
    uint8_t t;
    SWAP(z->b, z->b2);
    SWAP(z->c, z->c2);
    SWAP(z->d, z->d2);
    SWAP(z->e, z->e2);
    SWAP(z->h, z->h2);
    SWAP(z->l, z->l2);
#undef SWAP
}
/* PUSH / POP of a pair, through memory as the Z80's (high byte first) */
static inline void cpu_wr(Cpu *z, uint16_t a, uint8_t v);
static inline uint8_t cpu_rd(const Cpu *z, uint16_t a);
static inline void cpu_push(Cpu *z, uint16_t v)
{
    z->sp = (uint16_t)(z->sp - 1);
    cpu_wr(z, z->sp, (uint8_t)(v >> 8));
    z->sp = (uint16_t)(z->sp - 1);
    cpu_wr(z, z->sp, (uint8_t)v);
}
static inline uint16_t cpu_pop(Cpu *z)
{
    uint16_t v = (uint16_t)(cpu_rd(z, z->sp) | cpu_rd(z, (uint16_t)(z->sp + 1)) << 8);
    z->sp = (uint16_t)(z->sp + 2);
    return v;
}
#define AF(z) ((uint16_t)((z)->a << 8 | (z)->f))
static inline void cpu_set_af(Cpu *z, uint16_t v)
{
    z->a = (uint8_t)(v >> 8);
    z->f = (uint8_t)v;
}

/* An instruction with accesses: where they fall (cpu_access) */
static inline void cpu_insn(Cpu *z, const uint8_t *offs)
{
    z->partial = 0;
    z->acc = 0;
    z->offs = offs;
    z->t_start = z->t;
}

/* An instruction that would end past its budget: stopped at the first of
   its later fetches (`fo`, their offsets, 0 at the end) at or past it */
static inline void cpu_fetch(Cpu *z, uint16_t pc, const uint8_t *fo)
{
    for (; *fo; fo++)
        if (z->t + *fo >= z->limit) {
            z->stopped = 1;
            z->rep_n = 0;
            z->part_pc = pc;
            z->t_start = z->t;
            z->t_stop = z->t + *fo;
            memcpy(z->saved, z, offsetof(Cpu, regs_end));
            z->pc = pc;
            z->partial = 0;
            longjmp(*z->jb, 1);
        }
}

/* An instruction that would end past its budget: its registers kept, its
   accesses checked (a replay's too) */
static inline void cpu_part(Cpu *z, uint16_t pc, const uint8_t *offs)
{
    z->partial = 1;
    z->acc = 0;
    z->offs = offs;
    if (z->stopped && z->part_pc == pc) { /* run again after a stop: replay */
        z->stopped = 0;
        z->rep_now = z->rep_n;
        z->rep_n = 0;
    } else {
        z->rep_now = 0;
        z->rep_n = 0;
        z->part_pc = pc;
        z->t_start = z->t;
        memcpy(z->saved, z, offsetof(Cpu, regs_end));
    }
}

/* An access: replayed (1), or made now (0); one at or past the budget stops
   the program before it */
static inline int cpu_access(Cpu *z, uint8_t *v, int read)
{
    int k = z->acc++;
    if (k < z->rep_now) {
        if (read) *v = z->rep[k];
        return 1;
    }
    if (z->t_start + z->offs[k] >= z->limit) {
        z->stopped = 1;
        z->rep_n = (uint8_t)k;
        z->t_stop = z->t_start + z->offs[k];
        memcpy(z, z->saved, offsetof(Cpu, regs_end));
        z->t = z->t_start;
        z->pc = z->part_pc;
        z->partial = 0;
        longjmp(*z->jb, 1);
    }
    return 0;
}

/* A byte as the CPU reads it (its ROM through g->data, the rest its board's) */
static inline uint8_t cpu_rd_raw(const Cpu *z, uint16_t a)
{
    if (z->id == GAME_MAIN) return a < 0x8000 ? game_data(z->g, a) : game_rd(z->g, a);
    return a < 0x2000 ? game_data(z->g, GAME_SUB_ROM + a) : game_sub_rd(z->g, a);
}
static inline uint8_t cpu_rd(const Cpu *zc, uint16_t a)
{
    Cpu *z = (Cpu *)zc;
    uint8_t v;
    if (z->partial) {
        int k = z->acc;
        if (cpu_access(z, &v, 1)) return v;
        v = cpu_rd_raw(z, a);
        if (k < 8) z->rep[k] = v;
        return v;
    }
    z->acc++;
    return cpu_rd_raw(z, a);
}
static inline void cpu_wr(Cpu *z, uint16_t a, uint8_t v)
{
    int k = z->acc;
    if (z->partial) {
        if (cpu_access(z, NULL, 0)) return;
    } else {
        z->acc++;
    }
    z->t_access = z->offs ? z->t_start + z->offs[k] : z->t;
    if (z->id == GAME_MAIN) game_wr(z->g, a, v);
    else game_sub_wr(z->g, a, v);
}

/* The sub's I/O: IN A,(n) and OUT (n),A, their I/O cycle an access */
static inline uint8_t cpu_in(Cpu *z, uint8_t port)
{
    uint8_t v;
    int k = z->acc;
    if (z->partial) {
        if (cpu_access(z, &v, 1)) return v;
    } else {
        z->acc++;
    }
    z->t_access = z->offs ? z->t_start + z->offs[k] : z->t;
    v = z->g->io_read ? z->g->io_read(z->g->bus, port) : 0;
    if (z->partial && k < 8) z->rep[k] = v;
    return v;
}
static inline void cpu_out(Cpu *z, uint8_t port, uint8_t v)
{
    int k = z->acc;
    if (z->partial) {
        if (cpu_access(z, NULL, 0)) return;
    } else {
        z->acc++;
    }
    z->t_access = z->offs ? z->t_start + z->offs[k] : z->t;
    if (z->g->io_write) z->g->io_write(z->g->bus, port, v);
}
/* RETI's end: the daisy chain hears it */
static inline void cpu_reti(Cpu *z)
{
    z->t_access = z->t;
    if (z->id == GAME_SUB && z->g->reti) z->g->reti(z->g->bus);
}
/* (IX+d), (IY+d): WZ the address */
static inline uint8_t cpu_ix(Cpu *z, int d)
{
    z->wz = (uint16_t)(z->ix + d);
    return cpu_rd(z, z->wz);
}
static inline void cpu_set_ix(Cpu *z, int d, uint8_t v)
{
    z->wz = (uint16_t)(z->ix + d);
    cpu_wr(z, z->wz, v);
}
static inline uint8_t cpu_iy(Cpu *z, int d)
{
    z->wz = (uint16_t)(z->iy + d);
    return cpu_rd(z, z->wz);
}
static inline void cpu_set_iy(Cpu *z, int d, uint8_t v)
{
    z->wz = (uint16_t)(z->iy + d);
    cpu_wr(z, z->wz, v);
}
/* a word, low byte first */
static inline void cpu_wr16(Cpu *z, uint16_t a, uint16_t v)
{
    cpu_wr(z, a, (uint8_t)v);
    cpu_wr(z, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}

/* F's bits */
#define CPU_CF 0x01
#define CPU_NF 0x02
#define CPU_PF 0x04 /* parity / overflow */
#define CPU_XF 0x08 /* bit 3, undocumented */
#define CPU_HF 0x10
#define CPU_YF 0x20 /* bit 5, undocumented */
#define CPU_ZF 0x40
#define CPU_SF 0x80

/* S, Z and the undocumented bits of a result; with its parity */
static inline uint8_t cpu_sz(uint8_t v) { return (uint8_t)((v & (CPU_SF | CPU_YF | CPU_XF)) | (v ? 0 : CPU_ZF)); }
static inline uint8_t cpu_szp(uint8_t v)
{
    uint8_t p = v;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    return (uint8_t)(cpu_sz(v) | (p & 1 ? 0 : CPU_PF));
}
/* F set by the instruction ending at z->t (Q) */
static inline void cpu_f(Cpu *z, int f)
{
    z->f = (uint8_t)f;
    z->q_t = z->t;
}

/* the accumulator's operations */
static inline void cpu_add(Cpu *z, uint8_t v)
{
    int a = z->a, r = a + v;
    cpu_f(z, cpu_sz((uint8_t)r) | (r >> 8 & CPU_CF) | ((a ^ v ^ r) & CPU_HF) | (((a ^ ~v) & (a ^ r) & 0x80) >> 5));
    z->a = (uint8_t)r;
}
static inline void cpu_adc(Cpu *z, uint8_t v)
{
    int a = z->a, r = a + v + (z->f & CPU_CF);
    cpu_f(z, cpu_sz((uint8_t)r) | (r >> 8 & CPU_CF) | ((a ^ v ^ r) & CPU_HF) | (((a ^ ~v) & (a ^ r) & 0x80) >> 5));
    z->a = (uint8_t)r;
}
static inline int cpu_sub_f(Cpu *z, uint8_t v, int carry)
{
    int a = z->a, r = a - v - carry;
    cpu_f(z, cpu_sz((uint8_t)r) | CPU_NF | (r >> 8 & CPU_CF) | ((a ^ v ^ r) & CPU_HF) | (((a ^ v) & (a ^ r) & 0x80) >> 5));
    return r;
}
static inline void cpu_sub(Cpu *z, uint8_t v) { z->a = (uint8_t)cpu_sub_f(z, v, 0); }
static inline void cpu_sbc(Cpu *z, uint8_t v) { z->a = (uint8_t)cpu_sub_f(z, v, z->f & CPU_CF); }
/* CP: bits 3 and 5 from the operand */
static inline void cpu_cp(Cpu *z, uint8_t v)
{
    cpu_sub_f(z, v, 0);
    z->f = (uint8_t)((z->f & ~(CPU_YF | CPU_XF)) | (v & (CPU_YF | CPU_XF)));
}
static inline void cpu_and(Cpu *z, uint8_t v)
{
    z->a &= v;
    cpu_f(z, cpu_szp(z->a) | CPU_HF);
}
static inline void cpu_or(Cpu *z, uint8_t v)
{
    z->a |= v;
    cpu_f(z, cpu_szp(z->a));
}
static inline void cpu_xor(Cpu *z, uint8_t v)
{
    z->a ^= v;
    cpu_f(z, cpu_szp(z->a));
}
static inline void cpu_neg(Cpu *z)
{
    uint8_t v = z->a;
    z->a = 0;
    cpu_sub(z, v);
}
static inline void cpu_cpl(Cpu *z)
{
    z->a = (uint8_t)~z->a;
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF | CPU_CF)) | CPU_HF | CPU_NF | (z->a & (CPU_YF | CPU_XF)));
}
/* SCF, CCF: bits 3 and 5 from A or'd with F where the instruction before
   left F as it was (Q); z->t already past the 4 cycles */
static inline uint8_t cpu_q(const Cpu *z) { return z->q_t == z->t - 4 ? z->f : 0; }
static inline void cpu_scf(Cpu *z)
{
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | CPU_CF | (((cpu_q(z) ^ z->f) | z->a) & (CPU_YF | CPU_XF)));
}
static inline void cpu_ccf(Cpu *z)
{
    cpu_f(z, ((z->f & (CPU_SF | CPU_ZF | CPU_PF | CPU_CF)) | (z->f & CPU_CF) << 4 |
              (((cpu_q(z) ^ z->f) | z->a) & (CPU_YF | CPU_XF))) ^ CPU_CF);
}
static inline void cpu_daa(Cpu *z)
{
    uint8_t a = z->a, diff = 0, carry = z->f & CPU_CF;
    if (carry || a > 0x99) diff = 0x60, carry = CPU_CF;
    if ((z->f & CPU_HF) || (a & 0x0F) > 9) diff |= 0x06;
    uint8_t r = z->f & CPU_NF ? (uint8_t)(a - diff) : (uint8_t)(a + diff);
    uint8_t h = z->f & CPU_NF ? ((z->f & CPU_HF) && (a & 0x0F) < 6 ? CPU_HF : 0) : ((a & 0x0F) > 9 ? CPU_HF : 0);
    cpu_f(z, cpu_szp(r) | (z->f & CPU_NF) | h | carry);
    z->a = r;
}
/* INC r, DEC r: the carry kept */
static inline uint8_t cpu_inc(Cpu *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    cpu_f(z, (z->f & CPU_CF) | cpu_sz(r) | ((v & 0x0F) == 0x0F ? CPU_HF : 0) | (r == 0x80 ? CPU_PF : 0));
    return r;
}
static inline uint8_t cpu_dec(Cpu *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    cpu_f(z, (z->f & CPU_CF) | CPU_NF | cpu_sz(r) | ((v & 0x0F) == 0 ? CPU_HF : 0) | (v == 0x80 ? CPU_PF : 0));
    return r;
}
/* RLCA, RRCA, RLA, RRA */
static inline void cpu_rlca(Cpu *z)
{
    z->a = (uint8_t)(z->a << 1 | z->a >> 7);
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | (z->a & (CPU_YF | CPU_XF | CPU_CF)));
}
static inline void cpu_rrca(Cpu *z)
{
    uint8_t c = z->a & CPU_CF;
    z->a = (uint8_t)(z->a >> 1 | z->a << 7);
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | c | (z->a & (CPU_YF | CPU_XF)));
}
static inline void cpu_rla(Cpu *z)
{
    uint8_t c = z->a >> 7;
    z->a = (uint8_t)(z->a << 1 | (z->f & CPU_CF));
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | (z->a & (CPU_YF | CPU_XF)) | c);
}
static inline void cpu_rra(Cpu *z)
{
    uint8_t c = z->a & 1;
    z->a = (uint8_t)(z->a >> 1 | (z->f & CPU_CF) << 7);
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | (z->a & (CPU_YF | CPU_XF)) | c);
}
/* the CB rotates and shifts of a value */
static inline uint8_t cpu_rot(Cpu *z, uint8_t v, uint8_t c)
{
    cpu_f(z, cpu_szp(v) | c);
    return v;
}
static inline uint8_t cpu_rrc(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v >> 1 | v << 7), v & 1); }
static inline uint8_t cpu_rlc(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v << 1 | v >> 7), v >> 7); }
static inline uint8_t cpu_rl(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v << 1 | (z->f & CPU_CF)), v >> 7); }
static inline uint8_t cpu_rr(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v >> 1 | (z->f & CPU_CF) << 7), v & 1); }
static inline uint8_t cpu_sla(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v << 1), v >> 7); }
static inline uint8_t cpu_sra(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v >> 1 | (v & 0x80)), v & 1); }
static inline uint8_t cpu_srl(Cpu *z, uint8_t v) { return cpu_rot(z, (uint8_t)(v >> 1), v & 1); }
/* BIT n of a register (bits 3 and 5 from it), of memory (from WZ's high byte) */
static inline void cpu_bit_xy(Cpu *z, int n, uint8_t v, uint8_t xy)
{
    uint8_t r = v & (1 << n);
    cpu_f(z, (z->f & CPU_CF) | CPU_HF | (r ? 0 : CPU_ZF | CPU_PF) | (r & CPU_SF) | (xy & (CPU_YF | CPU_XF)));
}
static inline void cpu_bit(Cpu *z, int n, uint8_t v) { cpu_bit_xy(z, n, v, v); }
static inline void cpu_bit_mem(Cpu *z, int n, uint8_t v) { cpu_bit_xy(z, n, v, (uint8_t)(z->wz >> 8)); }
/* RLD, RRD: A's low nibble and (HL)'s */
static inline void cpu_rld(Cpu *z)
{
    uint8_t m = cpu_rd(z, HL(z));
    z->wz = (uint16_t)(HL(z) + 1);
    cpu_wr(z, HL(z), (uint8_t)(m << 4 | (z->a & 0x0F)));
    z->a = (uint8_t)((z->a & 0xF0) | m >> 4);
    cpu_f(z, (z->f & CPU_CF) | cpu_szp(z->a));
}
static inline void cpu_rrd(Cpu *z)
{
    uint8_t m = cpu_rd(z, HL(z));
    z->wz = (uint16_t)(HL(z) + 1);
    cpu_wr(z, HL(z), (uint8_t)(z->a << 4 | m >> 4));
    z->a = (uint8_t)((z->a & 0xF0) | (m & 0x0F));
    cpu_f(z, (z->f & CPU_CF) | cpu_szp(z->a));
}
/* LD A,I / LD A,R */
static inline void cpu_ld_a_ir(Cpu *z, uint8_t v)
{
    z->a = v;
    cpu_f(z, (z->f & CPU_CF) | cpu_sz(v) | (z->iff2 ? CPU_PF : 0));
}

/* 16 bits: ADD HL (IX, IY), ADC HL, SBC HL; WZ the first operand + 1 */
static inline uint16_t cpu_add16(Cpu *z, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    z->wz = (uint16_t)(a + 1);
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_PF)) | ((a ^ b ^ r) >> 8 & CPU_HF) | (r >> 16 & CPU_CF) |
                 (r >> 8 & (CPU_YF | CPU_XF)));
    return (uint16_t)r;
}
static inline uint16_t cpu_adc16(Cpu *z, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b + (z->f & CPU_CF);
    z->wz = (uint16_t)(a + 1);
    cpu_f(z, (int)((r >> 8 & (CPU_SF | CPU_YF | CPU_XF)) | ((r & 0xFFFF) ? 0 : CPU_ZF) | ((a ^ b ^ r) >> 8 & CPU_HF) |
                   (((a ^ ~b) & (a ^ r) & 0x8000) >> 13) | (r >> 16 & CPU_CF)));
    return (uint16_t)r;
}
static inline uint16_t cpu_sbc16(Cpu *z, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b - (z->f & CPU_CF);
    z->wz = (uint16_t)(a + 1);
    cpu_f(z, (int)((r >> 8 & (CPU_SF | CPU_YF | CPU_XF)) | ((r & 0xFFFF) ? 0 : CPU_ZF) | ((a ^ b ^ r) >> 8 & CPU_HF) |
                   (((a ^ b) & (a ^ r) & 0x8000) >> 13) | (r >> 16 & CPU_CF) | CPU_NF));
    return (uint16_t)r;
}

/* the block instructions (LDIR, LDDR, CPIR a step each, cpu_repeat when
   they go again) */
static inline void cpu_ld_step(Cpu *z, int step)
{
    uint8_t v = cpu_rd(z, HL(z));
    cpu_wr(z, DE(z), v);
    SET_HL(z, HL(z) + step);
    SET_DE(z, DE(z) + step);
    SET_BC(z, BC(z) - 1);
    uint8_t n = (uint8_t)(v + z->a);
    cpu_f(z, (z->f & (CPU_SF | CPU_ZF | CPU_CF)) | (n & CPU_XF) | (n << 4 & CPU_YF) | (BC(z) ? CPU_PF : 0));
}
static inline void cpu_ldi(Cpu *z) { cpu_ld_step(z, 1); }
static inline void cpu_ldd(Cpu *z) { cpu_ld_step(z, -1); }
static inline void cpu_cpi(Cpu *z)
{
    uint8_t v = cpu_rd(z, HL(z)), r = (uint8_t)(z->a - v);
    int h = (z->a ^ v ^ r) & CPU_HF;
    SET_HL(z, HL(z) + 1);
    SET_BC(z, BC(z) - 1);
    z->wz = (uint16_t)(z->wz + 1);
    uint8_t n = (uint8_t)(r - (h ? 1 : 0));
    cpu_f(z, (z->f & CPU_CF) | CPU_NF | (cpu_sz(r) & (CPU_SF | CPU_ZF)) | h | (n & CPU_XF) | (n << 4 & CPU_YF) |
                 (BC(z) ? CPU_PF : 0));
}
/* a block instruction at `pc` going again: bits 3 and 5 from its address */
static inline void cpu_repeat(Cpu *z, uint16_t pc)
{
    z->wz = (uint16_t)(pc + 1);
    z->f = (uint8_t)((z->f & ~(CPU_XF | CPU_YF)) | (pc >> 8 & (CPU_XF | CPU_YF)));
}

/* a word as the CPU reads it */
static inline uint16_t cpu_rd16(const Cpu *z, uint16_t a)
{
    return (uint16_t)(cpu_rd(z, a) | cpu_rd(z, (uint16_t)(a + 1)) << 8);
}

#endif
