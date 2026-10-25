#include "emu/z80.h"

/* Every memory access advances `t`, the T-state within the instruction, by
   its machine cycle (an opcode fetch 4, a read or write 3, an I/O cycle 4);
   IDLE adds the internal cycles where the Z80 spends them. The callbacks run
   at the start of their machine cycle: then c->t is its T-state. */
#define IDLE(n) (c->t += (n))

enum { CF = Z80_CF, NF = Z80_NF, PF = Z80_PF, XF = Z80_XF, HF = Z80_HF, YF = Z80_YF, ZF = Z80_ZF, SF = Z80_SF };

/* sz: S, Z and the undocumented bits of a result; szp: with its parity */
static uint8_t sz[256], szp[256];
static int tables_ready;

static void make_tables(void)
{
    for (int i = 0; i < 256; i++) {
        int bits = 0;
        for (int k = 0; k < 8; k++) bits += i >> k & 1;
        sz[i] = (uint8_t)((i & (SF | YF | XF)) | (i ? 0 : ZF));
        szp[i] = (uint8_t)(sz[i] | (bits & 1 ? 0 : PF));
    }
    tables_ready = 1;
}

void z80_reset(Z80 *c)
{
    if (!tables_ready) make_tables();
    c->pc = 0;
    c->i = c->r = 0;
    c->iff1 = c->iff2 = 0;
    c->im = 0;
    c->halted = 0;
    c->ei_delay = 0;
    c->nmi_pending = 0;
    c->wz = 0;
    c->t = 0;
    /* the registers hold garbage at power-on; as MAME's: AF $0040, the index
       registers $FFFF, the others 0 */
    c->a = 0x00, c->f = 0x40;
    c->b = c->c = c->d = c->e = c->h = c->l = 0;
    c->a2 = c->f2 = c->b2 = c->c2 = c->d2 = c->e2 = c->h2 = c->l2 = 0;
    c->ixh = c->ixl = c->iyh = c->iyl = 0xFF;
    c->sp = 0;
}

void z80_set_nmi(Z80 *c, int state)
{
    if (state && !c->nmi_line) c->nmi_pending = 1;
    c->nmi_line = state;
}

/* ---- machine cycles ---------------------------------------------------------- */

static void refresh(Z80 *c) { c->r = (uint8_t)((c->r & 0x80) | ((c->r + 1) & 0x7F)); }

static uint8_t m1(Z80 *c)
{
    refresh(c);
    uint8_t v = c->read(c->ctx, c->pc++);
    c->t += 4;
    return v;
}

static uint8_t rd(Z80 *c, uint16_t a)
{
    uint8_t v = c->read(c->ctx, a);
    c->t += 3;
    return v;
}

static void wr(Z80 *c, uint16_t a, uint8_t v)
{
    c->write(c->ctx, a, v);
    c->t += 3;
}

static uint8_t port_in(Z80 *c, uint16_t port)
{
    uint8_t v = c->in ? c->in(c->ctx, port) : 0xFF;
    c->t += 4;
    return v;
}

static void port_out(Z80 *c, uint16_t port, uint8_t v)
{
    if (c->out) c->out(c->ctx, port, v);
    c->t += 4;
}

static uint8_t imm(Z80 *c) { return rd(c, c->pc++); }

static uint16_t imm16(Z80 *c)
{
    uint8_t lo = imm(c);
    return (uint16_t)(lo | imm(c) << 8);
}

static uint16_t rd16(Z80 *c, uint16_t a)
{
    uint8_t lo = rd(c, a);
    return (uint16_t)(lo | rd(c, (uint16_t)(a + 1)) << 8);
}

static void wr16(Z80 *c, uint16_t a, uint16_t v)
{
    wr(c, a, v & 0xFF);
    wr(c, (uint16_t)(a + 1), v >> 8);
}

static void push(Z80 *c, uint16_t v)
{
    wr(c, --c->sp, v >> 8);
    wr(c, --c->sp, v & 0xFF);
}

static uint16_t pop(Z80 *c)
{
    uint8_t lo = rd(c, c->sp++);
    return (uint16_t)(lo | rd(c, c->sp++) << 8);
}

/* ---- registers ------------------------------------------------------------------ */

#define BC() ((uint16_t)(c->b << 8 | c->c))
#define DE() ((uint16_t)(c->d << 8 | c->e))
#define HL() ((uint16_t)(c->h << 8 | c->l))
#define SET_BC(v) (c->b = (uint8_t)((v) >> 8), c->c = (uint8_t)(v))
#define SET_DE(v) (c->d = (uint8_t)((v) >> 8), c->e = (uint8_t)(v))
#define SET_HL(v) (c->h = (uint8_t)((v) >> 8), c->l = (uint8_t)(v))

/* pfx: 0 none, 1 DD (IX), 2 FD (IY) */
static uint16_t get_xy(Z80 *c, int pfx)
{
    return pfx == 1 ? (uint16_t)(c->ixh << 8 | c->ixl) : pfx == 2 ? (uint16_t)(c->iyh << 8 | c->iyl) : HL();
}

static void set_xy(Z80 *c, int pfx, uint16_t v)
{
    if (pfx == 1) c->ixh = (uint8_t)(v >> 8), c->ixl = (uint8_t)v;
    else if (pfx == 2) c->iyh = (uint8_t)(v >> 8), c->iyl = (uint8_t)v;
    else SET_HL(v);
}

/* register r (0-7, not 6) under a prefix: H and L become the index's halves */
static uint8_t *reg(Z80 *c, int r, int pfx)
{
    switch (r) {
    case 0: return &c->b;
    case 1: return &c->c;
    case 2: return &c->d;
    case 3: return &c->e;
    case 4: return pfx == 1 ? &c->ixh : pfx == 2 ? &c->iyh : &c->h;
    case 5: return pfx == 1 ? &c->ixl : pfx == 2 ? &c->iyl : &c->l;
    default: return &c->a;
    }
}

static uint16_t get_rp(Z80 *c, int p, int pfx)
{
    switch (p) {
    case 0: return BC();
    case 1: return DE();
    case 2: return get_xy(c, pfx);
    default: return c->sp;
    }
}

static void set_rp(Z80 *c, int p, int pfx, uint16_t v)
{
    switch (p) {
    case 0: SET_BC(v); break;
    case 1: SET_DE(v); break;
    case 2: set_xy(c, pfx, v); break;
    default: c->sp = v; break;
    }
}

static int cond(Z80 *c, int y)
{
    switch (y) {
    case 0: return !(c->f & ZF);
    case 1: return c->f & ZF;
    case 2: return !(c->f & CF);
    case 3: return c->f & CF;
    case 4: return !(c->f & PF);
    case 5: return c->f & PF;
    case 6: return !(c->f & SF);
    default: return c->f & SF;
    }
}

/* the address of (HL), or of (IX+d) / (IY+d): d is read, then 5 cycles
   adding it (`n` of them overlap reading an immediate byte that follows:
   LD (IX+d),n); sets WZ for the index */
static uint16_t mem_addr(Z80 *c, int pfx)
{
    if (!pfx) return HL();
    uint16_t a = (uint16_t)(get_xy(c, pfx) + (int8_t)imm(c));
    c->wz = a;
    return a;
}

/* ---- arithmetic ------------------------------------------------------------ */

static void alu(Z80 *c, int op, uint8_t v)
{
    int a = c->a, r, carry = c->f & CF;
    switch (op) {
    case 0: /* ADD */
    case 1: /* ADC */
        r = a + v + (op == 1 ? carry : 0);
        c->q_set = 1, c->f = (uint8_t)(sz[r & 0xFF] | (r >> 8 & CF) | ((a ^ v ^ r) & HF) | (((a ^ ~v) & (a ^ r) & 0x80) >> 5));
        c->a = (uint8_t)r;
        break;
    case 2: /* SUB */
    case 3: /* SBC */
    case 7: /* CP */
        r = a - v - (op == 3 ? carry : 0);
        c->q_set = 1, c->f = (uint8_t)(sz[r & 0xFF] | NF | (r >> 8 & CF) | ((a ^ v ^ r) & HF) | (((a ^ v) & (a ^ r) & 0x80) >> 5));
        if (op == 7) c->q_set = 1, c->f = (uint8_t)((c->f & ~(YF | XF)) | (v & (YF | XF))); /* CP: bits 3, 5 from the operand */
        else c->a = (uint8_t)r;
        break;
    case 4: /* AND */
        c->a &= v;
        c->q_set = 1, c->f = (uint8_t)(szp[c->a] | HF);
        break;
    case 5: /* XOR */
        c->a ^= v;
        c->q_set = 1, c->f = szp[c->a];
        break;
    default: /* OR */
        c->a |= v;
        c->q_set = 1, c->f = szp[c->a];
        break;
    }
}

static uint8_t inc8(Z80 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    c->q_set = 1, c->f = (uint8_t)((c->f & CF) | sz[r] | ((v & 0x0F) == 0x0F ? HF : 0) | (r == 0x80 ? PF : 0));
    return r;
}

static uint8_t dec8(Z80 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    c->q_set = 1, c->f = (uint8_t)((c->f & CF) | NF | sz[r] | ((v & 0x0F) == 0 ? HF : 0) | (v == 0x80 ? PF : 0));
    return r;
}

/* CB rotates and shifts: RLC RRC RL RR SLA SRA SLL SRL */
static uint8_t rot(Z80 *c, int op, uint8_t v)
{
    int r, carry;
    switch (op) {
    case 0: r = v << 1 | v >> 7, carry = v >> 7; break;
    case 1: r = v >> 1 | v << 7, carry = v & 1; break;
    case 2: r = v << 1 | (c->f & CF), carry = v >> 7; break;
    case 3: r = v >> 1 | (c->f & CF) << 7, carry = v & 1; break;
    case 4: r = v << 1, carry = v >> 7; break;
    case 5: r = v >> 1 | (v & 0x80), carry = v & 1; break;
    case 6: r = v << 1 | 1, carry = v >> 7; break;
    default: r = v >> 1, carry = v & 1; break;
    }
    c->q_set = 1, c->f = (uint8_t)(szp[r & 0xFF] | carry);
    return (uint8_t)r;
}

static void bit(Z80 *c, int n, uint8_t v, uint8_t xy)
{
    uint8_t r = v & (1 << n);
    c->q_set = 1, c->f = (uint8_t)((c->f & CF) | HF | (r ? 0 : ZF | PF) | (r & SF) | (xy & (YF | XF)));
}

static void add16(Z80 *c, int pfx, uint16_t v)
{
    uint16_t a = get_xy(c, pfx);
    uint32_t r = (uint32_t)a + v;
    c->wz = (uint16_t)(a + 1);
    c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | ((a ^ v ^ r) >> 8 & HF) | (r >> 16 & CF) | (r >> 8 & (YF | XF)));
    set_xy(c, pfx, (uint16_t)r);
}

static void adc16(Z80 *c, uint16_t v)
{
    uint16_t a = HL();
    uint32_t r = (uint32_t)a + v + (c->f & CF);
    c->wz = (uint16_t)(a + 1);
    c->q_set = 1, c->f = (uint8_t)((r >> 8 & (SF | YF | XF)) | ((r & 0xFFFF) ? 0 : ZF) | ((a ^ v ^ r) >> 8 & HF) |
                     (((a ^ ~v) & (a ^ r) & 0x8000) >> 13) | (r >> 16 & CF));
    SET_HL(r);
}

static void sbc16(Z80 *c, uint16_t v)
{
    uint16_t a = HL();
    uint32_t r = (uint32_t)a - v - (c->f & CF);
    c->wz = (uint16_t)(a + 1);
    c->q_set = 1, c->f = (uint8_t)((r >> 8 & (SF | YF | XF)) | ((r & 0xFFFF) ? 0 : ZF) | ((a ^ v ^ r) >> 8 & HF) |
                     (((a ^ v) & (a ^ r) & 0x8000) >> 13) | (r >> 16 & CF) | NF);
    SET_HL(r);
}

static void daa(Z80 *c)
{
    uint8_t a = c->a, diff = 0, carry = c->f & CF;
    if (carry || a > 0x99) diff = 0x60, carry = CF;
    if ((c->f & HF) || (a & 0x0F) > 9) diff |= 0x06;
    uint8_t r = c->f & NF ? (uint8_t)(a - diff) : (uint8_t)(a + diff);
    uint8_t h = c->f & NF ? ((c->f & HF) && (a & 0x0F) < 6 ? HF : 0) : ((a & 0x0F) > 9 ? HF : 0);
    c->q_set = 1, c->f = (uint8_t)(szp[r] | (c->f & NF) | h | carry);
    c->a = r;
}

/* ---- prefixed groups --------------------------------------------------------- */

/* CB xx (the CB fetched), or DD/FD CB d xx: the operation is read as data,
   the result also goes to register z unless z is 6, and BIT takes bits 3
   and 5 from the address's high byte */
static void exec_cb(Z80 *c, int pfx)
{
    uint16_t addr = 0;
    uint8_t op;
    if (pfx) {
        addr = mem_addr(c, pfx);
        op = imm(c);
        IDLE(2);
    } else {
        op = m1(c);
    }
    int x = op >> 6, y = op >> 3 & 7, z = op & 7;
    int mem = pfx || z == 6;
    if (!pfx && mem) addr = HL();
    uint8_t v, r;
    if (mem) {
        v = rd(c, addr);
        IDLE(1);
    } else {
        v = *reg(c, z, 0);
    }
    switch (x) {
    case 0: r = rot(c, y, v); break;
    case 1: bit(c, y, v, mem ? (uint8_t)(c->wz >> 8) : v); return;
    case 2: r = (uint8_t)(v & ~(1 << y)); break;
    default: r = (uint8_t)(v | 1 << y); break;
    }
    if (mem) {
        wr(c, addr, r);
        if (pfx && z != 6) *reg(c, z, 0) = r;
    } else {
        *reg(c, z, 0) = r;
    }
}

/* LDIR, LDDR, CPIR and CPDR going round again: X and Y are the
   instruction address's bits 11 and 13 */
static void repeat_flags(Z80 *c, uint16_t pc)
{
    c->q_set = 1, c->f = (uint8_t)((c->f & ~(XF | YF)) | (pc >> 8 & (XF | YF)));
}

/* the block group's flags for INI, IND, OUTI, OUTD */
static void io_block_flags(Z80 *c, uint8_t v, int k)
{
    c->q_set = 1, c->f = (uint8_t)(sz[c->b] | (v & 0x80 ? NF : 0) | (k > 0xFF ? HF | CF : 0) | (szp[(uint8_t)((k & 7) ^ c->b)] & PF));
}

static void exec_ed(Z80 *c)
{
    uint8_t op = m1(c);
    int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    uint16_t start = (uint16_t)(c->pc - 2);
    if (x == 1) {
        switch (z) {
        case 0: { /* IN r,(C); y = 6: flags only */
            uint8_t v = port_in(c, BC());
            c->wz = (uint16_t)(BC() + 1);
            if (y != 6) *reg(c, y, 0) = v;
            c->q_set = 1, c->f = (uint8_t)((c->f & CF) | szp[v]);
            return;
        }
        case 1: /* OUT (C),r; y = 6 outputs 0 */
            port_out(c, BC(), y == 6 ? 0 : *reg(c, y, 0));
            c->wz = (uint16_t)(BC() + 1);
            return;
        case 2:
            IDLE(7);
            if (q) adc16(c, get_rp(c, p, 0));
            else sbc16(c, get_rp(c, p, 0));
            return;
        case 3: {
            uint16_t a = imm16(c);
            if (q) set_rp(c, p, 0, rd16(c, a));
            else wr16(c, a, get_rp(c, p, 0));
            c->wz = (uint16_t)(a + 1);
            return;
        }
        case 4: { /* NEG */
            uint8_t v = c->a;
            c->a = 0;
            alu(c, 2, v);
            return;
        }
        case 5: /* RETN, RETI */
            c->pc = pop(c);
            c->wz = c->pc;
            c->iff1 = c->iff2;
            if (y == 1 && c->reti) c->reti(c->ctx);
            return;
        case 6:
            c->im = (y & 3) == 0 || (y & 3) == 1 ? 0 : (y & 3) == 2 ? 1 : 2;
            return;
        default:
            switch (y) {
            case 0: IDLE(1); c->i = c->a; return;
            case 1: IDLE(1); c->r = c->a; return;
            case 2:
            case 3:
                IDLE(1);
                c->a = y == 2 ? c->i : c->r;
                c->q_set = 1, c->f = (uint8_t)((c->f & CF) | sz[c->a] | (c->iff2 ? PF : 0));
                return;
            case 4: { /* RRD */
                uint8_t m = rd(c, HL());
                c->wz = (uint16_t)(HL() + 1);
                IDLE(4);
                wr(c, HL(), (c->a << 4 | m >> 4) & 0xFF);
                c->a = (uint8_t)((c->a & 0xF0) | (m & 0x0F));
                c->q_set = 1, c->f = (uint8_t)((c->f & CF) | szp[c->a]);
                return;
            }
            case 5: { /* RLD */
                uint8_t m = rd(c, HL());
                c->wz = (uint16_t)(HL() + 1);
                IDLE(4);
                wr(c, HL(), (m << 4 | (c->a & 0x0F)) & 0xFF);
                c->a = (uint8_t)((c->a & 0xF0) | m >> 4);
                c->q_set = 1, c->f = (uint8_t)((c->f & CF) | szp[c->a]);
                return;
            }
            default: return; /* ED 77, ED 7F: no operation */
            }
        }
    }
    if (x == 2 && z <= 3 && y >= 4) { /* the block group */
        int repeat = y >= 6;
        int step = y & 1 ? -1 : 1;
        switch (z) {
        case 0: { /* LDI LDD LDIR LDDR */
            uint8_t v = rd(c, HL());
            wr(c, DE(), v);
            IDLE(2);
            SET_HL(HL() + step);
            SET_DE(DE() + step);
            SET_BC(BC() - 1);
            uint8_t n = (uint8_t)(v + c->a);
            c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | CF)) | (n & XF) | (n << 4 & YF) | (BC() ? PF : 0));
            if (repeat && BC()) {
                IDLE(5);
                c->pc = start;
                c->wz = (uint16_t)(start + 1);
                repeat_flags(c, start);
            }
            return;
        }
        case 1: { /* CPI CPD CPIR CPDR */
            uint8_t v = rd(c, HL());
            IDLE(5);
            uint8_t r = (uint8_t)(c->a - v);
            int h = (c->a ^ v ^ r) & HF;
            SET_HL(HL() + step);
            SET_BC(BC() - 1);
            c->wz = (uint16_t)(c->wz + step);
            uint8_t n = (uint8_t)(r - (h ? 1 : 0));
            c->q_set = 1, c->f = (uint8_t)((c->f & CF) | NF | (sz[r] & (SF | ZF)) | h | (n & XF) | (n << 4 & YF) | (BC() ? PF : 0));
            if (repeat && BC() && r) {
                IDLE(5);
                c->pc = start;
                c->wz = (uint16_t)(start + 1);
                repeat_flags(c, start);
            }
            return;
        }
        case 2: { /* INI IND INIR INDR */
            IDLE(1);
            uint8_t v = port_in(c, BC());
            c->wz = (uint16_t)(BC() + step);
            wr(c, HL(), v);
            c->b--;
            SET_HL(HL() + step);
            io_block_flags(c, v, v + (uint8_t)(c->c + step));
            if (repeat && c->b) {
                IDLE(5);
                c->pc = start;
            }
            return;
        }
        default: { /* OUTI OUTD OTIR OTDR */
            IDLE(1);
            uint8_t v = rd(c, HL());
            c->b--;
            c->wz = (uint16_t)(BC() + step);
            port_out(c, BC(), v);
            SET_HL(HL() + step);
            io_block_flags(c, v, v + c->l);
            if (repeat && c->b) {
                IDLE(5);
                c->pc = start;
            }
            return;
        }
        }
    }
    /* the rest of ED: no operation */
}

/* ---- the main table ---------------------------------------------------------- */

static void exec(Z80 *c, uint8_t op, int pfx)
{
    int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: return;
            case 1: {
                uint8_t t = c->a;
                c->a = c->a2, c->a2 = t;
                t = c->f, c->f = c->f2, c->f2 = t;
                return;
            }
            case 2: { /* DJNZ */
                IDLE(1);
                int8_t d = (int8_t)imm(c);
                if (--c->b) {
                    IDLE(5);
                    c->pc = (uint16_t)(c->pc + d);
                    c->wz = c->pc;
                }
                return;
            }
            default: { /* JR, JR cc */
                int8_t d = (int8_t)imm(c);
                if (y == 3 || cond(c, y - 4)) {
                    IDLE(5);
                    c->pc = (uint16_t)(c->pc + d);
                    c->wz = c->pc;
                }
                return;
            }
            }
        case 1:
            if (!q) set_rp(c, p, pfx, imm16(c));
            else {
                IDLE(7);
                add16(c, pfx, get_rp(c, p, pfx));
            }
            return;
        case 2:
            switch (y) {
            case 0:
            case 2: { /* LD (BC),A / LD (DE),A */
                uint16_t a = y ? DE() : BC();
                wr(c, a, c->a);
                c->wz = (uint16_t)(c->a << 8 | ((a + 1) & 0xFF));
                return;
            }
            case 1:
            case 3: { /* LD A,(BC) / LD A,(DE) */
                uint16_t a = y == 3 ? DE() : BC();
                c->a = rd(c, a);
                c->wz = (uint16_t)(a + 1);
                return;
            }
            case 4: {
                uint16_t a = imm16(c);
                wr16(c, a, get_xy(c, pfx));
                c->wz = (uint16_t)(a + 1);
                return;
            }
            case 5: {
                uint16_t a = imm16(c);
                set_xy(c, pfx, rd16(c, a));
                c->wz = (uint16_t)(a + 1);
                return;
            }
            case 6: {
                uint16_t a = imm16(c);
                wr(c, a, c->a);
                c->wz = (uint16_t)(c->a << 8 | ((a + 1) & 0xFF));
                return;
            }
            default: {
                uint16_t a = imm16(c);
                c->a = rd(c, a);
                c->wz = (uint16_t)(a + 1);
                return;
            }
            }
        case 3:
            IDLE(2);
            set_rp(c, p, pfx, (uint16_t)(get_rp(c, p, pfx) + (q ? -1 : 1)));
            return;
        case 4:
        case 5:
            if (y == 6) {
                uint16_t a = mem_addr(c, pfx);
                if (pfx) IDLE(5);
                uint8_t v = rd(c, a);
                IDLE(1);
                wr(c, a, z == 4 ? inc8(c, v) : dec8(c, v));
            } else {
                uint8_t *r = reg(c, y, pfx);
                *r = z == 4 ? inc8(c, *r) : dec8(c, *r);
            }
            return;
        case 6:
            if (y == 6) {
                uint16_t a = mem_addr(c, pfx);
                uint8_t n = imm(c);
                if (pfx) IDLE(2); /* the rest of the index's 5 under reading n */
                wr(c, a, n);
                return;
            }
            *reg(c, y, pfx) = imm(c);
            return;
        default:
            switch (y) {
            case 0: /* RLCA */
                c->a = (uint8_t)(c->a << 1 | c->a >> 7);
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | (c->a & (YF | XF | CF)));
                return;
            case 1: /* RRCA */
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | (c->a & CF));
                c->a = (uint8_t)(c->a >> 1 | c->a << 7);
                c->f |= c->a & (YF | XF);
                return;
            case 2: { /* RLA */
                uint8_t carry = c->a >> 7;
                c->a = (uint8_t)(c->a << 1 | (c->f & CF));
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | (c->a & (YF | XF)) | carry);
                return;
            }
            case 3: { /* RRA */
                uint8_t carry = c->a & 1;
                c->a = (uint8_t)(c->a >> 1 | (c->f & CF) << 7);
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | (c->a & (YF | XF)) | carry);
                return;
            }
            case 4: daa(c); return;
            case 5: /* CPL */
                c->a = (uint8_t)~c->a;
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF | CF)) | HF | NF | (c->a & (YF | XF)));
                return;
            case 6: /* SCF; X and Y: A or'd with F where the last instruction left F as it was (Q) */
                c->q_set = 1, c->f = (uint8_t)((c->f & (SF | ZF | PF)) | CF | (((c->q ^ c->f) | c->a) & (YF | XF)));
                return;
            default: /* CCF */
                c->q_set = 1, c->f = (uint8_t)(((c->f & (SF | ZF | PF | CF)) | (c->f & CF) << 4 | (((c->q ^ c->f) | c->a) & (YF | XF))) ^ CF);
                return;
            }
        }
    case 1:
        if (op == 0x76) { /* HALT: the CPU runs NOPs at this address until an interrupt */
            c->halted = 1;
            c->pc--;
            return;
        }
        if (y == 6) {
            uint16_t a = mem_addr(c, pfx);
            if (pfx) IDLE(5);
            wr(c, a, *reg(c, z, 0));
            return;
        }
        if (z == 6) {
            uint16_t a = mem_addr(c, pfx);
            if (pfx) IDLE(5);
            *reg(c, y, 0) = rd(c, a);
            return;
        }
        *reg(c, y, pfx) = *reg(c, z, pfx);
        return;
    case 2:
        if (z == 6) {
            uint16_t a = mem_addr(c, pfx);
            if (pfx) IDLE(5);
            alu(c, y, rd(c, a));
            return;
        }
        alu(c, y, *reg(c, z, pfx));
        return;
    default:
        switch (z) {
        case 0:
            IDLE(1);
            if (cond(c, y)) {
                c->pc = pop(c);
                c->wz = c->pc;
            }
            return;
        case 1:
            if (!q) {
                uint16_t v = pop(c);
                if (p == 3) c->a = (uint8_t)(v >> 8), c->f = (uint8_t)v;
                else set_rp(c, p, pfx, v);
                return;
            }
            switch (p) {
            case 0:
                c->pc = pop(c);
                c->wz = c->pc;
                return;
            case 1: { /* EXX */
                uint8_t t;
#define SWAP(r) (t = c->r, c->r = c->r##2, c->r##2 = t)
                SWAP(b), SWAP(c), SWAP(d), SWAP(e), SWAP(h), SWAP(l);
#undef SWAP
                return;
            }
            case 2: c->pc = get_xy(c, pfx); return;
            default: IDLE(2); c->sp = get_xy(c, pfx); return;
            }
        case 2: {
            uint16_t a = imm16(c);
            c->wz = a;
            if (cond(c, y)) c->pc = a;
            return;
        }
        case 3:
            switch (y) {
            case 0:
                c->pc = imm16(c);
                c->wz = c->pc;
                return;
            case 1: exec_cb(c, pfx); return;
            case 2: {
                uint8_t n = imm(c);
                port_out(c, (uint16_t)(c->a << 8 | n), c->a);
                c->wz = (uint16_t)(c->a << 8 | ((n + 1) & 0xFF));
                return;
            }
            case 3: {
                uint8_t n = imm(c);
                uint16_t port = (uint16_t)(c->a << 8 | n);
                c->a = port_in(c, port);
                c->wz = (uint16_t)(port + 1);
                return;
            }
            case 4: { /* EX (SP),HL */
                uint8_t lo = rd(c, c->sp);
                uint8_t hi = rd(c, (uint16_t)(c->sp + 1));
                IDLE(1);
                uint16_t old = get_xy(c, pfx);
                wr(c, (uint16_t)(c->sp + 1), old >> 8);
                wr(c, c->sp, old & 0xFF);
                IDLE(2);
                set_xy(c, pfx, (uint16_t)(hi << 8 | lo));
                c->wz = (uint16_t)(hi << 8 | lo);
                return;
            }
            case 5: { /* EX DE,HL: never the index */
                uint8_t t = c->d;
                c->d = c->h, c->h = t;
                t = c->e, c->e = c->l, c->l = t;
                return;
            }
            case 6:
                c->iff1 = c->iff2 = 0;
                return;
            default:
                c->iff1 = c->iff2 = 1;
                c->ei_delay = 1;
                return;
            }
        case 4: {
            uint16_t a = imm16(c);
            c->wz = a;
            if (cond(c, y)) {
                IDLE(1);
                push(c, c->pc);
                c->pc = a;
            }
            return;
        }
        case 5:
            if (!q) {
                IDLE(1);
                push(c, p == 3 ? (uint16_t)(c->a << 8 | c->f) : get_rp(c, p, pfx));
                return;
            }
            switch (p) {
            case 0: {
                uint16_t a = imm16(c);
                c->wz = a;
                IDLE(1);
                push(c, c->pc);
                c->pc = a;
                return;
            }
            case 1: exec(c, m1(c), 1); return;
            case 2: exec_ed(c); return;
            default: exec(c, m1(c), 2); return;
            }
        case 6: alu(c, y, imm(c)); return;
        default:
            IDLE(1);
            push(c, c->pc);
            c->pc = (uint16_t)(y * 8);
            c->wz = c->pc;
            return;
        }
    }
}

/* ---- interrupts and the step -------------------------------------------------- */

static void leave_halt(Z80 *c)
{
    if (c->halted) {
        c->halted = 0;
        c->pc++;
    }
}

int z80_step(Z80 *c)
{
    c->t = 0;
    c->q_set = 0;
    c->last_interrupt = 0;
    if (c->nmi_pending) {
        c->nmi_pending = 0;
        leave_halt(c);
        refresh(c);
        c->iff1 = 0;
        IDLE(5);
        push(c, c->pc);
        c->pc = 0x0066;
        c->wz = c->pc;
        c->last_interrupt = 1;
    } else if (c->irq_line && c->iff1 && !c->ei_delay) {
        leave_halt(c);
        refresh(c);
        c->iff1 = c->iff2 = 0;
        c->last_interrupt = 1;
        uint8_t vector = c->irq_ack ? c->irq_ack(c->ctx) : c->irq_vector;
        IDLE(7); /* the acknowledge cycle, with its two wait states */
        push(c, c->pc);
        if (c->im == 2) c->pc = rd16(c, (uint16_t)(c->i << 8 | vector));
        /* IM 1 is RST 38; IM 0 runs the byte on the bus (an RST on every board here) */
        else c->pc = c->im == 1 ? 0x0038 : (uint16_t)(vector & 0x38);
        c->wz = c->pc;
    } else {
        c->ei_delay = 0;
        if (c->halted) {
            refresh(c);
            IDLE(4);
        } else {
            exec(c, m1(c), 0);
        }
    }
    c->q = c->q_set ? c->f : 0;
    c->cycles += c->t;
    return c->t;
}
