/*
 * The Z80 core on its own (always runs): every opcode's T-states (each
 * group's documented counts, branches taken and not), the machine-cycle
 * timing adding up to them, and known results of instructions whose flags
 * are easy to get wrong (DAA, the overflow and half carries, CP's and BIT's
 * undocumented bits, the block group, 16-bit arithmetic), interrupts and HALT.
 */
#include <stdio.h>
#include <string.h>

#include "emu/z80.h"

static int failures;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

static uint8_t mem[0x10000];
static int last_t; /* the T-state of the last access the CPU made */
static Z80 cpu;

static uint8_t rd(void *ctx, uint16_t a) { last_t = cpu.t; return mem[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { last_t = cpu.t; mem[a] = v; }

/* a CPU at $1000 with F = 0 (NZ, NC, PO and P hold), BC = 0, HL = $2000 */
static void setup(const uint8_t *code, int n)
{
    memset(&cpu, 0, sizeof cpu);
    cpu.read = rd;
    cpu.write = wr;
    z80_reset(&cpu);
    cpu.f = 0;
    cpu.pc = 0x1000;
    cpu.sp = 0x8000;
    cpu.h = 0x20, cpu.l = 0x00;
    cpu.ixh = cpu.iyh = 0x30, cpu.ixl = cpu.iyl = 0x00;
    memset(mem, 0, sizeof mem);
    memcpy(mem + 0x1000, code, (size_t)n);
    mem[0x2000] = 0x55; /* CPIR's (HL) differs from A */
}

/* The main table with F = 0, B = 0: conditions NZ NC PO P true, Z C PE M false. */
static const uint8_t main_t[256] = {
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,      /* 00 */
    13, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,    /* 10: DJNZ taken (B 0 -> FF) */
    12, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,   /* 20: JR NZ taken, JR Z not */
    12, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4, /* 30 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* 40 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* 50 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* 60 */
    7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,        /* 70: HALT 4 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* 80 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* 90 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* A0 */
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,        /* B0 */
    11, 10, 10, 10, 17, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11, /* C0: CB apart */
    11, 10, 10, 11, 17, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,  /* D0: DD apart */
    11, 10, 10, 19, 17, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,   /* E0: ED apart */
    11, 10, 10, 4, 17, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11,    /* F0: FD apart */
};

/* (HL) memory operands: under DD / FD they take (IX+d), 8 more (LD (IX+d),n 5 more) */
static int hl_memory(int op)
{
    if (op == 0x34 || op == 0x35 || op == 0x36) return 1;
    if (op == 0x76) return 0;
    if (op >= 0x40 && op < 0x80) return (op & 7) == 6 || (op & 0x38) == 0x30;
    if (op >= 0x80 && op < 0xC0) return (op & 7) == 6;
    return 0;
}

static int ed_t(int op)
{
    if (op >= 0x40 && op < 0x80) {
        static const uint8_t row[8] = { 12, 12, 15, 20, 8, 14, 8, 0 };
        if ((op & 7) != 7) return row[op & 7];
        static const uint8_t z7[8] = { 9, 9, 9, 9, 18, 18, 8, 8 };
        return z7[op >> 3 & 7];
    }
    if (op >= 0xA0 && op < 0xC0 && (op & 7) <= 3) return op >= 0xB0 ? 21 : 16; /* the repeats repeat here */
    return 8;
}

/* Run one instruction; its T-states, which must also be where the machine
   cycles ended. */
static int run(const uint8_t *code, int n)
{
    setup(code, n);
    int t = z80_step(&cpu);
    CHECK(t == cpu.t, "step returned %d, cycles counted %d", t, cpu.t);
    return t;
}

static void timing(void)
{
    for (int op = 0; op < 256; op++) {
        if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) continue;
        uint8_t code[4] = { (uint8_t)op, 0, 0, 0 };
        int t = run(code, 4);
        CHECK(t == main_t[op], "%02X: %d T, want %d", op, t, main_t[op]);
        if (op == 0x76) continue;
        for (int pfx = 0xDD; pfx <= 0xFD; pfx += 0x20) {
            uint8_t ix[5] = { (uint8_t)pfx, (uint8_t)op, 0, 0, 0 };
            int want = 4 + main_t[op] + (hl_memory(op) ? (op == 0x36 ? 5 : 8) : 0);
            if (op == 0xEB || op == 0xD9) want = 4 + main_t[op]; /* EX DE,HL and EXX: never the index */
            t = run(ix, 5);
            CHECK(t == want, "%02X %02X: %d T, want %d", pfx, op, t, want);
        }
    }
    for (int op = 0; op < 256; op++) {
        uint8_t cb[2] = { 0xCB, (uint8_t)op };
        int mem_op = (op & 7) == 6, is_bit = (op >> 6) == 1;
        int want = !mem_op ? 8 : is_bit ? 12 : 15;
        int t = run(cb, 2);
        CHECK(t == want, "CB %02X: %d T, want %d", op, t, want);
        uint8_t ddcb[4] = { 0xDD, 0xCB, 0x05, (uint8_t)op };
        t = run(ddcb, 4);
        CHECK(t == (is_bit ? 20 : 23), "DD CB d %02X: %d T", op, t);
        uint8_t ed[4] = { 0xED, (uint8_t)op, 0, 0 };
        t = run(ed, 4);
        CHECK(t == ed_t(op), "ED %02X: %d T, want %d", op, t, ed_t(op));
    }
    /* where the writes fall: LD (nn),A at T 10, PUSH at 5 and 8, CALL at 11 and 14 */
    static const uint8_t ld[3] = { 0x32, 0x00, 0x40 };
    run(ld, 3);
    CHECK(last_t == 10, "LD (nn),A writes at T %d", last_t);
    static const uint8_t pushbc[1] = { 0xC5 };
    run(pushbc, 1);
    CHECK(last_t == 8, "PUSH's second write at T %d", last_t);
    static const uint8_t call[3] = { 0xCD, 0x00, 0x30 };
    run(call, 3);
    CHECK(last_t == 14, "CALL's second write at T %d", last_t);
    static const uint8_t ldir[2] = { 0xED, 0xB0 };
    run(ldir, 2);
    CHECK(last_t == 11, "LDIR writes at T %d", last_t);
}

/* Run code from $1000 until PC reaches `end` (at most 1000 steps). */
static void exec_to(const uint8_t *code, int n, uint16_t end)
{
    memcpy(mem + 0x1000, code, (size_t)n);
    for (int i = 0; i < 1000 && cpu.pc != end; i++) z80_step(&cpu);
    CHECK(cpu.pc == end, "ran to $%04X, not $%04X", cpu.pc, end);
}

static void results(void)
{
    /* ADD: $7F + 1 overflows to $80 with a half carry */
    static const uint8_t add[] = { 0x3E, 0x7F, 0xC6, 0x01 };
    setup(add, 0), exec_to(add, sizeof add, 0x1004);
    CHECK(cpu.a == 0x80 && cpu.f == 0x94, "ADD 7F+1: A %02X F %02X", cpu.a, cpu.f);
    /* SUB: 0 - 1 borrows everywhere */
    static const uint8_t sub[] = { 0x3E, 0x00, 0xD6, 0x01 };
    setup(sub, 0), exec_to(sub, sizeof sub, 0x1004);
    CHECK(cpu.a == 0xFF && cpu.f == 0xBB, "SUB 0-1: A %02X F %02X", cpu.a, cpu.f);
    /* CP: bits 3 and 5 from the operand, not the result */
    static const uint8_t cp[] = { 0x3E, 0x10, 0xFE, 0x28 };
    setup(cp, 0), exec_to(cp, sizeof cp, 0x1004);
    CHECK(cpu.a == 0x10 && cpu.f == 0xBB, "CP 10,28: F %02X", cpu.f);
    /* DAA after a BCD addition: 15 + 27 = 42 */
    static const uint8_t daa[] = { 0x3E, 0x15, 0xC6, 0x27, 0x27 };
    setup(daa, 0), exec_to(daa, sizeof daa, 0x1005);
    CHECK(cpu.a == 0x42 && cpu.f == 0x14, "DAA 15+27: A %02X F %02X", cpu.a, cpu.f);
    /* DAA after a BCD subtraction: 42 - 15 = 27 */
    static const uint8_t das[] = { 0x3E, 0x42, 0xD6, 0x15, 0x27 };
    setup(das, 0), exec_to(das, sizeof das, 0x1005);
    CHECK(cpu.a == 0x27 && (cpu.f & (Z80_NF | Z80_CF)) == Z80_NF, "DAA 42-15: A %02X F %02X", cpu.a, cpu.f);
    /* NEG of $80 */
    static const uint8_t neg[] = { 0x3E, 0x80, 0xED, 0x44 };
    setup(neg, 0), exec_to(neg, sizeof neg, 0x1004);
    CHECK(cpu.a == 0x80 && cpu.f == 0x87, "NEG 80: A %02X F %02X", cpu.a, cpu.f);
    /* BIT n,(HL): bits 3 and 5 from WZ's high byte (LD A,(nn) left nn + 1) */
    static const uint8_t bitw[] = { 0x3A, 0x00, 0x28, 0x21, 0x00, 0x30, 0xCB, 0x46 };
    setup(bitw, 0), exec_to(bitw, sizeof bitw, 0x1008);
    CHECK(cpu.f == 0x7C, "BIT 0,(HL) after LD A,($2800): F %02X", cpu.f);
    /* BIT 7,r: S from the bit, bits 3 and 5 from r */
    static const uint8_t bitr[] = { 0x06, 0xA8, 0xCB, 0x78 };
    setup(bitr, 0), exec_to(bitr, sizeof bitr, 0x1004);
    CHECK(cpu.f == (Z80_SF | Z80_HF | Z80_YF | Z80_XF), "BIT 7,B (A8): F %02X", cpu.f);
    /* RLD */
    static const uint8_t rld[] = { 0x3E, 0x12, 0x21, 0x00, 0x30, 0x36, 0x34, 0xED, 0x6F };
    setup(rld, 0), exec_to(rld, sizeof rld, 0x1009);
    CHECK(cpu.a == 0x13 && mem[0x3000] == 0x42, "RLD: A %02X (HL) %02X", cpu.a, mem[0x3000]);
    /* LDI: bits 3 and 5 from (HL) + A, P/V while BC is not 0 */
    static const uint8_t ldi[] = { 0x3E, 0x00, 0x01, 0x02, 0x00, 0x21, 0x00, 0x30, 0x11, 0x00, 0x31, 0x36, 0x0A, 0xED, 0xA0 };
    setup(ldi, 0), exec_to(ldi, sizeof ldi, 0x100F);
    CHECK(mem[0x3100] == 0x0A && cpu.f == 0x2C && cpu.c == 1, "LDI: F %02X BC %02X%02X", cpu.f, cpu.b, cpu.c);
    /* LDIR copies BC bytes */
    static const uint8_t ldir[] = { 0x21, 0x00, 0x30, 0x11, 0x00, 0x31, 0x01, 0x10, 0x00, 0xED, 0xB0 };
    setup(ldir, 0);
    for (int i = 0; i < 16; i++) mem[0x3000 + i] = (uint8_t)(i * 7 + 1);
    exec_to(ldir, sizeof ldir, 0x100B);
    CHECK(!memcmp(mem + 0x3000, mem + 0x3100, 16) && !cpu.b && !cpu.c && !(cpu.f & Z80_PF), "LDIR");
    /* CPIR stops at the match */
    static const uint8_t cpir[] = { 0x21, 0x00, 0x30, 0x01, 0x10, 0x00, 0x3E, 0x08, 0xED, 0xB1 };
    setup(cpir, 0);
    for (int i = 0; i < 16; i++) mem[0x3000 + i] = (uint8_t)i;
    exec_to(cpir, sizeof cpir, 0x100A);
    CHECK(cpu.l == 0x09 && cpu.c == 0x07 && (cpu.f & Z80_ZF), "CPIR: HL %02X%02X BC %02X%02X F %02X", cpu.h, cpu.l,
          cpu.b, cpu.c, cpu.f);
    /* ADC HL: $7FFF + 1 overflows */
    static const uint8_t adc[] = { 0x21, 0xFF, 0x7F, 0x11, 0x01, 0x00, 0xB7, 0xED, 0x5A };
    setup(adc, 0), exec_to(adc, sizeof adc, 0x1009);
    CHECK(cpu.h == 0x80 && cpu.l == 0 && cpu.f == 0x94, "ADC HL: %02X%02X F %02X", cpu.h, cpu.l, cpu.f);
    /* SBC HL: 0 - 1 */
    static const uint8_t sbc[] = { 0x21, 0x00, 0x00, 0x11, 0x01, 0x00, 0xB7, 0xED, 0x52 };
    setup(sbc, 0), exec_to(sbc, sizeof sbc, 0x1009);
    CHECK(cpu.h == 0xFF && cpu.l == 0xFF && cpu.f == 0xBB, "SBC HL: %02X%02X F %02X", cpu.h, cpu.l, cpu.f);
    /* ADD HL keeps S, Z, P/V; H from bit 11 */
    static const uint8_t addhl[] = { 0x3E, 0x00, 0xB7, 0x21, 0x00, 0x0F, 0x01, 0x00, 0x01, 0x09 };
    setup(addhl, 0), exec_to(addhl, sizeof addhl, 0x100A);
    CHECK(cpu.h == 0x10 && cpu.f == (Z80_ZF | Z80_PF | Z80_HF), "ADD HL: %02X%02X F %02X", cpu.h, cpu.l, cpu.f);
    /* the index registers: LD (IX+d),n; INC (IX+d); the halves */
    static const uint8_t ix[] = { 0xDD, 0x21, 0x00, 0x30, 0xDD, 0x36, 0xFE, 0x41, 0xDD, 0x34, 0xFE, 0xDD, 0x26, 0x12,
                                  0xDD, 0x7C, 0xFD, 0x21, 0x34, 0x12, 0xFD, 0x7D };
    setup(ix, 0), exec_to(ix, sizeof ix, 0x1000 + sizeof ix);
    CHECK(mem[0x2FFE] == 0x42 && cpu.ixh == 0x12 && cpu.a == 0x34, "index: (IX-2) %02X IXH %02X A %02X",
          mem[0x2FFE], cpu.ixh, cpu.a);
    /* DD CB: SET 3,(IX+1) also copies the result into B */
    static const uint8_t ddcb[] = { 0xDD, 0x21, 0x00, 0x30, 0xDD, 0xCB, 0x01, 0xD8 };
    setup(ddcb, 0), exec_to(ddcb, sizeof ddcb, 0x1008);
    CHECK(mem[0x3001] == 0x08 && cpu.b == 0x08, "DD CB SET: (IX+1) %02X B %02X", mem[0x3001], cpu.b);
    /* CALL, PUSH, POP, EX (SP),HL, RET */
    static const uint8_t calls[] = { 0xCD, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, /* $1010: */ 0x21, 0xCD, 0xAB, 0xE5, 0xC1, 0xE3, 0xE3, 0xC9 };
    setup(calls, 0), exec_to(calls, sizeof calls, 0x1003);
    CHECK(cpu.b == 0xAB && cpu.c == 0xCD && cpu.sp == 0x8000 && cpu.h == 0xAB, "calls: BC %02X%02X SP %04X HL %02X%02X",
          cpu.b, cpu.c, cpu.sp, cpu.h, cpu.l);
    /* DJNZ counts down */
    static const uint8_t djnz[] = { 0x06, 0x05, 0x3C, 0x10, 0xFD };
    setup(djnz, 0), exec_to(djnz, sizeof djnz, 0x1005);
    CHECK(cpu.a == 5 && cpu.b == 0, "DJNZ: A %d", cpu.a);
    /* the refresh register counts opcode fetches (two for a prefixed one), keeping bit 7 */
    static const uint8_t refr[] = { 0x3E, 0x80, 0xED, 0x4F, 0x00, 0xDD, 0x00, 0xED, 0x5F };
    setup(refr, 0), exec_to(refr, sizeof refr, 0x1009);
    CHECK(cpu.a == 0x85, "LD A,R: %02X", cpu.a);
}

static void interrupts(void)
{
    /* an NMI: PC pushed, $0066, IFF1 off; RETN brings IFF1 back */
    static const uint8_t prog[] = { 0xFB, 0x00, 0x00 };
    setup(prog, sizeof prog);
    mem[0x66] = 0xED, mem[0x67] = 0x45;
    z80_step(&cpu); /* EI */
    z80_set_nmi(&cpu, 1);
    int t = z80_step(&cpu);
    CHECK(t == 11 && cpu.pc == 0x66 && !cpu.iff1 && cpu.iff2 && mem[0x7FFE] == 0x01 && mem[0x7FFF] == 0x10,
          "NMI: %d T, PC %04X", t, cpu.pc);
    z80_step(&cpu); /* RETN */
    CHECK(cpu.pc == 0x1001 && cpu.iff1, "RETN: PC %04X IFF1 %d", cpu.pc, cpu.iff1);
    z80_step(&cpu);
    CHECK(cpu.pc == 0x1002, "the line held: no second NMI");
    /* HALT runs NOPs at its address; an NMI leaves it past the HALT */
    static const uint8_t halt[] = { 0x76, 0x00 };
    setup(halt, sizeof halt);
    z80_step(&cpu);
    z80_step(&cpu);
    CHECK(cpu.pc == 0x1000 && cpu.halted, "HALT: PC %04X", cpu.pc);
    z80_set_nmi(&cpu, 1);
    z80_step(&cpu);
    CHECK(cpu.pc == 0x66 && mem[0x7FFE] == 0x01 && mem[0x7FFF] == 0x10, "NMI out of HALT returns to $%02X%02X",
          mem[0x7FFF], mem[0x7FFE]);
    /* IM 1 waits a step after EI */
    static const uint8_t im1[] = { 0xED, 0x56, 0xFB, 0x00, 0x00 };
    setup(im1, sizeof im1);
    cpu.irq_line = 1;
    z80_step(&cpu); /* IM 1 */
    z80_step(&cpu); /* EI */
    z80_step(&cpu); /* the NOP after EI runs first */
    CHECK(cpu.pc == 0x1004, "after EI: PC %04X", cpu.pc);
    t = z80_step(&cpu);
    CHECK(t == 13 && cpu.pc == 0x38, "IM 1: %d T, PC %04X", t, cpu.pc);
}

int main(void)
{
    timing();
    results();
    interrupts();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("z80: all checks passed");
    return 0;
}
