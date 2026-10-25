#include "emu/board.h"

#include <string.h>

/* ---- time ------------------------------------------------------------------------ */

#define AS_PER_SECOND 1000000000000000000LL
#define MAIN_APC 250000000000LL      /* attoseconds a cycle at 4 MHz */
#define SUB_APC 500000000000LL       /* at 2 MHz */
#define QUANTUM_AS (AS_PER_SECOND / 60) /* MAME's slices when the driver sets none: 1/60 s */
#define PIXEL_AS 166666666666LL      /* 6 MHz (MAME's, truncated) */
#define LINE_AS (PIXEL_AS * 384)
#define FRAME_AS (LINE_AS * BOARD_LINES)
#define SOUND_TIMER_AS 20000000000000000LL /* MAME's sound update, 50 Hz */

static const int64_t apc[BOARD_CPUS] = { MAIN_APC, SUB_APC };

static Atime at_make(int64_t sec, int64_t as)
{
    sec += as / AS_PER_SECOND;
    as %= AS_PER_SECOND;
    if (as < 0) {
        as += AS_PER_SECOND;
        sec--;
    }
    return (Atime){ sec, as };
}

static Atime at_add_as(Atime t, int64_t as) { return at_make(t.sec, t.as + as); }
static int at_lt(Atime a, Atime b) { return a.sec < b.sec || (a.sec == b.sec && a.as < b.as); }
static Atime at_max(Atime a, Atime b) { return at_lt(a, b) ? b : a; }
/* a - b in attoseconds (the two are within a second or so of each other) */
static int64_t at_diff(Atime a, Atime b) { return (a.sec - b.sec) * AS_PER_SECOND + (a.as - b.as); }
static const Atime NEVER = { INT64_MAX / 2, 0 };

/* n periods of p attoseconds from t, without overflowing */
static Atime at_mul_add(Atime t, int64_t n, int64_t p)
{
    int64_t per_sec = AS_PER_SECOND / p; /* whole periods a second, roughly */
    if (per_sec < 1) per_sec = 1;
    while (n > per_sec) {
        t = at_add_as(t, per_sec * p);
        n -= per_sec;
    }
    return at_add_as(t, n * p);
}

int64_t board_total_cycles(const Board *b, int cpu)
{
    return b->total[cpu] + (b->executing == cpu ? b->run_cycles - b->icount : 0);
}

/* ---- timers ---------------------------------------------------------------------- */

enum { T_VBLANK, T_LINE0, T_VBLANK_END, T_SOUND, T_CTC0, T_SYNC0 = T_CTC0 + 4 };

static Atime next_expire(const Board *b)
{
    Atime t = NEVER;
    for (int i = 0; i < BOARD_TIMERS; i++)
        if (b->timers[i].active && at_lt(b->timers[i].expire, t)) t = b->timers[i].expire;
    return t;
}

/* The time the running CPU is at (its access's machine cycle); in a timer's
   callback, the timer's time; else the base. */
static Atime now(const Board *b)
{
    int c = b->executing;
    if (b->in_callback) return b->callback_time;
    if (c < 0) return b->base;
    int64_t done = b->z80[c].t - b->t0;
    return at_add_as(b->local[c], (b->run_cycles - b->icount + done) * apc[c]);
}

/* A timer that becomes the first due, set while a CPU runs, ends the slice
   after the CPU's instruction (MAME's abort of the timeslice). */
static void timer_set(Board *b, int i, Atime at, int64_t period, void (*fire)(Board *, int), int param)
{
    BoardTimer *t = &b->timers[i];
    t->active = 0;
    if (b->executing >= 0 && at_lt(at, next_expire(b)) && !b->abort_slice) {
        b->abort_slice = 1;
        b->abort_t = b->z80[b->executing].t;
    }
    t->expire = at;
    t->period = (Atime){ 0, period };
    t->active = 1;
    t->fire = fire;
    t->param = param;
    t->seq = ++b->timer_seq;
}

/* MAME's synchronize(): a timer now, running fire (if any) when the other
   CPUs have caught up. */
static void sync_now(Board *b, void (*fire)(Board *, int), int param)
{
    for (int i = T_SYNC0; i < BOARD_TIMERS; i++)
        if (!b->timers[i].active) {
            timer_set(b, i, now(b), 0, fire, param);
            return;
        }
}

/* Timers due by the base time, in order (equal times: in the order set). */
static void run_timers(Board *b)
{
    for (;;) {
        int due = -1;
        for (int i = 0; i < BOARD_TIMERS; i++) {
            BoardTimer *t = &b->timers[i];
            if (!t->active || at_lt(b->base, t->expire)) continue;
            if (due < 0 || at_lt(t->expire, b->timers[due].expire) ||
                (!at_lt(b->timers[due].expire, t->expire) && t->seq < b->timers[due].seq))
                due = i;
        }
        if (due < 0) return;
        BoardTimer *t = &b->timers[due];
        void (*fire)(Board *, int) = t->fire;
        int param = t->param;
        Atime when = t->expire;
        if (t->period.as) {
            t->expire = at_add_as(t->expire, t->period.as);
            t->seq = ++b->timer_seq;
        } else {
            t->active = 0;
        }
        b->in_callback = 1;
        b->callback_time = when;
        if (fire) fire(b, param);
        b->in_callback = 0;
    }
}

/* ---- interrupt lines ----------------------------------------------------------- */

/* A Z80 stopped inside an instruction sees line changes once it is done */
static void set_irq(Board *b, int c, int state)
{
    if (b->part[c].active) b->part[c].irq = state;
    else b->z80[c].irq_line = state;
}

static void irq_lines(Board *b, int unused)
{
    set_irq(b, CPU_MAIN, b->irq_main);
    set_irq(b, CPU_SUB, b->irq_sub);
}

/* The sub's daisy chain, highest priority first: the PIO's port A, then the
   CTC's channels. A device asks for an interrupt when pending and enabled,
   unless one before it (or itself) is under service. */
static int daisy_request(const Board *b)
{
    if (b->pio.ius) return 0;
    if (b->pio.ip && b->pio.ie) return 1;
    for (int i = 0; i < 4; i++) {
        const CtcChannel *ch = &b->ctc.ch[i];
        if (ch->ius) return 0;
        if (ch->ip && (ch->control & 0x80)) return 1;
    }
    return 0;
}

/* The line follows the chain; a change takes effect once the CPUs have
   caught up (MAME's set_input_line). */
static void update_sub_irq(Board *b)
{
    int state = daisy_request(b);
    if (state != b->irq_sub) {
        b->irq_sub = state;
        sync_now(b, irq_lines, 0);
    }
}

/* ---- the CTC ------------------------------------------------------------------- */

static int64_t ctc_period(const CtcChannel *ch)
{
    int prescale = ch->control & 0x20 ? 256 : 16;
    int tc = ch->tc ? ch->tc : 256;
    return (int64_t)prescale * tc * SUB_APC;
}

static void ctc_trigger(Board *b, int i);

/* A channel's zero count: its interrupt, and its ZC/TO pulse (channel 0's
   clocks channel 1, channel 2's steps the DAC) */
static void ctc_zero(Board *b, int i)
{
    CtcChannel *ch = &b->ctc.ch[i];
    if (ch->control & 0x80) {
        ch->ip = 1;
        update_sub_irq(b);
    }
    if (i == 0) ctc_trigger(b, 1);
    else if (i == 2 && b->sound_count < BOARD_SOUND_EVENTS) {
        SoundEvent *e = &b->sound[b->sound_count++];
        e->at = at_diff(now(b), b->frame_start);
        e->chip = 5;
        e->value = 0;
    }
}

static void ctc_timer(Board *b, int i) { ctc_zero(b, i); }

/* A pulse on a channel's CLK/TRG input: counter mode counts it */
static void ctc_trigger(Board *b, int i)
{
    CtcChannel *ch = &b->ctc.ch[i];
    if (!ch->running || !(ch->control & 0x40)) return;
    if (--ch->down <= 0) {
        ch->down = ch->tc ? ch->tc : 256;
        ctc_zero(b, i);
    }
}

static void ctc_write(Board *b, int i, uint8_t v)
{
    CtcChannel *ch = &b->ctc.ch[i];
    if (ch->want_tc) {
        ch->want_tc = 0;
        ch->tc = v;
        if (!ch->running) {
            ch->running = 1;
            if (ch->control & 0x40) {
                ch->down = v ? v : 256;
            } else {
                int64_t p = ctc_period(ch);
                timer_set(b, T_CTC0 + i, at_add_as(now(b), p), p, ctc_timer, i);
            }
        }
        return;
    }
    if (!(v & 1)) {
        if (i == 0) b->ctc.vector = v & 0xF8;
        return;
    }
    ch->control = v;
    if (v & 2) {
        ch->running = 0;
        b->timers[T_CTC0 + i].active = 0;
    }
    if (v & 4) ch->want_tc = 1;
    update_sub_irq(b);
}

/* ---- the PIO and the sound latch ----------------------------------------------- */

static void pio_control(Board *b, int port_b, uint8_t v)
{
    if (port_b) {
        int k = !(v & 1) ? 0 : (v & 0x0F) == 0x0F ? 1 : 2;
        b->pio.ctrl_b[k] = v;
        return;
    }
    if (b->pio.want_mask) {
        b->pio.want_mask = 0;
        b->pio.mask = v;
    } else if (!(v & 1)) {
        b->pio.vector = v;
    } else if ((v & 0x0F) == 0x0F) {
        b->pio.mode = v >> 6;
    } else if ((v & 0x0F) == 0x07) {
        b->pio.icw = v;
        b->pio.ie = v >> 7;
        b->pio.want_mask = (v & 0x10) != 0;
    } else if ((v & 0x0F) == 0x03) {
        b->pio.ie = v >> 7;
    }
    update_sub_irq(b);
}

/* A latch write reaches the sub once it has caught up: the PIO's strobe
   (the first write since power-on strobes nothing, MAME's) */
static void latch_sync(Board *b, int v)
{
    b->latch = (uint8_t)v;
    if (b->latch_written) {
        b->pio.ip = 1;
        update_sub_irq(b);
    }
    b->latch_written = 1;
}

/* ---- memory ---------------------------------------------------------------------- */

static uint8_t main_raw(Board *b, uint16_t a)
{
    if (a < 0x8000) return b->rom->main[a];
    if (a < 0x9000) return b->ram[a & 0xFFF];
    if (a < 0x9400) return b->fgvideo[a & 0x3FF];
    if (a < 0x9800) return b->fgcolor[a & 0x3FF];
    if (a < 0x9880) return b->sprites[a & 0x7F];
    if (a >= 0x9C00 && a < 0x9E00) return b->palette[a & 0x1FF];
    if (a >= 0x9E00 && a < 0x9E40) return b->regs[a & 0x3F];
    if (a >= 0xA000 && a < 0xA800) return b->bg3[a & 0x7FF];
    if (a >= 0xA800 && a < 0xB000) return b->bg2[a & 0x7FF];
    if (a >= 0xB000 && a < 0xB800) return b->bg1[a & 0x7FF];
    if (a >= 0xB800 && a < 0xBC00) return b->radar[a & 0x3FF];
    switch (a) {
    case 0xD000: return b->p1;
    case 0xD001: return b->p2;
    case 0xD002: return b->system;
    case 0xD004: return b->dsw1;
    case 0xD005: return b->dsw2;
    default: return 0;
    }
}

static uint8_t sub_raw(Board *b, uint16_t a)
{
    if (a < 0x2000) return b->rom->sub[a];
    if (a >= 0x4000 && a < 0x4400) return b->subram[a & 0x3FF];
    return 0;
}

uint8_t board_peek(Board *b, uint16_t addr) { return main_raw(b, addr); }

/* A Z80's access: replayed (1), or made now (0); one whose machine cycle
   starts once the slice's cycles are spent stops the CPU before it. */
static int z80_access(Board *b, int c, uint8_t *value)
{
    if (b->executing != c) return 0;
    if (b->replay) {
        b->replay--;
        *value = b->part[c].value[b->access++];
        return 1;
    }
    int t = b->z80[c].t;
    if (t - b->t0 >= b->icount_start || (b->abort_t >= 0 && t > b->abort_t)) {
        b->part[c].done = b->access;
        b->part[c].t = t;
        b->part[c].active = 1;
        longjmp(b->stop, 1);
    }
    return 0;
}

static void record(Board *b, int c, uint8_t v)
{
    if (b->executing == c && b->access < 16) b->part[c].value[b->access] = v;
    b->access++;
}

static uint8_t read_main(void *ctx, uint16_t a)
{
    Board *b = ctx;
    uint8_t v;
    if (z80_access(b, CPU_MAIN, &v)) return v;
    v = main_raw(b, a);
    if (b->read_hook) b->read_hook(b, CPU_MAIN, a, v);
    record(b, CPU_MAIN, v);
    return v;
}

static void write_main(void *ctx, uint16_t a, uint8_t v)
{
    Board *b = ctx;
    uint8_t unused;
    if (z80_access(b, CPU_MAIN, &unused)) return; /* made before the CPU stopped */
    b->access++;
    if (b->write_hook) b->write_hook(b, CPU_MAIN, a, v);
    if (a < 0x8000) return;
    if (a < 0x9000) b->ram[a & 0xFFF] = v;
    else if (a < 0x9400) b->fgvideo[a & 0x3FF] = v;
    else if (a < 0x9800) b->fgcolor[a & 0x3FF] = v;
    else if (a < 0x9880) b->sprites[a & 0x7F] = v;
    else if (a >= 0x9C00 && a < 0x9E00) b->palette[a & 0x1FF] = v;
    else if (a >= 0x9E00 && a < 0x9E40) b->regs[a & 0x3F] = v;
    else if (a >= 0xA000 && a < 0xA800) b->bg3[a & 0x7FF] = v;
    else if (a >= 0xA800 && a < 0xB000) b->bg2[a & 0x7FF] = v;
    else if (a >= 0xB000 && a < 0xB800) b->bg1[a & 0x7FF] = v;
    else if (a >= 0xB800 && a < 0xBC00) b->radar[a & 0x3FF] = v;
    else if (a == 0xD000) b->flip = v & 1;
    else if (a == 0xD002) {
        b->irq_main = 0;
        sync_now(b, irq_lines, 0);
    } else if (a == 0xD004) sync_now(b, latch_sync, v);
}

static uint8_t read_sub(void *ctx, uint16_t a)
{
    Board *b = ctx;
    uint8_t v;
    if (z80_access(b, CPU_SUB, &v)) return v;
    v = sub_raw(b, a);
    if (b->read_hook) b->read_hook(b, CPU_SUB, a, v);
    record(b, CPU_SUB, v);
    return v;
}

static void sound_event(Board *b, int chip, uint8_t v)
{
    if (b->sound_count == BOARD_SOUND_EVENTS) return;
    SoundEvent *e = &b->sound[b->sound_count++];
    e->at = at_diff(now(b), b->frame_start);
    e->chip = (uint8_t)chip;
    e->value = v;
}

static void write_sub(void *ctx, uint16_t a, uint8_t v)
{
    Board *b = ctx;
    uint8_t unused;
    if (z80_access(b, CPU_SUB, &unused)) return;
    b->access++;
    if (b->write_hook) b->write_hook(b, CPU_SUB, a, v);
    if (a >= 0x4000 && a < 0x4400) b->subram[a & 0x3FF] = v;
    else if (a == 0x8000) sound_event(b, 0, v);
    else if (a == 0x9000) sound_event(b, 1, v);
    else if (a == 0xA000) sound_event(b, 2, v);
    else if (a == 0xD000) sound_event(b, 3, v);
    else if (a == 0xE000) sound_event(b, 4, v);
}

static uint8_t in_sub(void *ctx, uint16_t port)
{
    Board *b = ctx;
    uint8_t v;
    if (z80_access(b, CPU_SUB, &v)) return v;
    switch (port & 0xFF) {
    case 0x00: v = b->latch; break;     /* port A: the sound latch */
    case 0x02: v = b->pio.out_b; break;
    default: v = 0; break;
    }
    if (b->read_hook) b->read_hook(b, CPU_SUB, 0x10000 | (port & 0xFF), v);
    record(b, CPU_SUB, v);
    return v;
}

static void out_sub(void *ctx, uint16_t port, uint8_t v)
{
    Board *b = ctx;
    uint8_t unused;
    if (z80_access(b, CPU_SUB, &unused)) return;
    b->access++;
    if (b->write_hook) b->write_hook(b, CPU_SUB, 0x10000 | (port & 0xFF), v);
    int p = port & 0xFF;
    if (p < 4) {
        if (p & 1) pio_control(b, p & 2, v);
        else if (p & 2) b->pio.out_b = v;
    } else if (p >= 8 && p < 12) {
        ctc_write(b, p & 3, v);
    }
}

/* The acknowledge: the first device asking gives its vector and is under
   service until RETI */
static uint8_t ack_sub(void *ctx)
{
    Board *b = ctx;
    uint8_t v;
    if (z80_access(b, CPU_SUB, &v)) return v;
    v = 0xFF;
    if (!b->pio.ius && b->pio.ip && b->pio.ie) {
        b->pio.ip = 0;
        b->pio.ius = 1;
        v = b->pio.vector;
    } else {
        for (int i = 0; i < 4 && !b->pio.ius; i++) {
            CtcChannel *ch = &b->ctc.ch[i];
            if (ch->ius) break;
            if (ch->ip && (ch->control & 0x80)) {
                ch->ip = 0;
                ch->ius = 1;
                v = (uint8_t)(b->ctc.vector | i << 1);
                break;
            }
        }
    }
    record(b, CPU_SUB, v);
    update_sub_irq(b);
    return v;
}

/* RETI ends the service of the first device under service */
static void reti_sub(void *ctx)
{
    Board *b = ctx;
    if (b->pio.ius) {
        b->pio.ius = 0;
    } else {
        for (int i = 0; i < 4; i++)
            if (b->ctc.ch[i].ius) {
                b->ctc.ch[i].ius = 0;
                break;
            }
    }
    update_sub_irq(b);
}

/* ---- sound ----------------------------------------------------------------------- */

#define TICK_AS (AS_PER_SECOND / SOUND_TICK_HZ)

static int64_t ticks(Atime t) { return t.sec * SOUND_TICK_HZ + t.as / TICK_AS; }

/* the frame's sound, from the writes' times in it, to its end */
static void make_sound(Board *b, Atime end)
{
    int64_t start = ticks(b->frame_start);
    b->audio_count = 0;
    for (int i = 0; i < b->sound_count; i++) {
        const SoundEvent *e = &b->sound[i];
        b->audio_count += sound_run(&b->chips, start + e->at / TICK_AS, b->audio + b->audio_count,
                                    BOARD_AUDIO_MAX - b->audio_count);
        sound_write(&b->chips, e->chip, e->value);
    }
    b->audio_count += sound_run(&b->chips, ticks(end), b->audio + b->audio_count, BOARD_AUDIO_MAX - b->audio_count);
    b->sound_count = 0;
}

/* ---- the screen's events --------------------------------------------------------- */

static void vblank(Board *b, int unused)
{
    make_sound(b, b->timers[T_VBLANK].expire);
    b->frame_start = b->timers[T_VBLANK].expire;
    b->frame_done = 1;
    b->irq_main = 1;
    sync_now(b, irq_lines, 0);
    timer_set(b, T_VBLANK, at_add_as(b->timers[T_VBLANK].expire, FRAME_AS), 0, vblank, 0);
}

static void line0(Board *b, int unused)
{
    timer_set(b, T_LINE0, at_add_as(b->timers[T_LINE0].expire, FRAME_AS), 0, line0, 0);
}

static void vblank_end(Board *b, int unused)
{
    timer_set(b, T_VBLANK_END, at_add_as(b->timers[T_VBLANK_END].expire, FRAME_AS), 0, vblank_end, 0);
}

static void sound_tick(Board *b, int unused) {}

/* ---- power-on and the slices ---------------------------------------------------- */

void board_init(Board *b, const RomSet *rom)
{
    memset(b, 0, sizeof *b);
    b->rom = rom;
    b->dsw1 = DSW1_DEFAULT;
    b->dsw2 = DSW2_DEFAULT;
    b->executing = -1;
    Z80 *z = b->z80;
    z[0].read = read_main, z[0].write = write_main;
    z[1].read = read_sub, z[1].write = write_sub;
    z[1].in = in_sub, z[1].out = out_sub;
    z[1].irq_ack = ack_sub, z[1].reti = reti_sub;
    for (int i = 0; i < BOARD_CPUS; i++) {
        z[i].ctx = b;
        z80_reset(&z[i]);
    }
    b->pio.ip = 1; /* pending from power-on (MAME's) */
    sound_init(&b->chips, rom->dac);
    /* the screen starts at the vertical blank */
    timer_set(b, T_VBLANK, (Atime){ 0, FRAME_AS }, 0, vblank, 0);
    timer_set(b, T_LINE0, (Atime){ 0, (BOARD_LINES - BOARD_VBLANK_LINE) * LINE_AS }, 0, line0, 0);
    timer_set(b, T_VBLANK_END, (Atime){ 0, (BOARD_LINES - BOARD_VBLANK_LINE + 16) * LINE_AS }, 0, vblank_end, 0);
    timer_set(b, T_SOUND, (Atime){ 0, SOUND_TIMER_AS }, SOUND_TIMER_AS, sound_tick, 0);
}

/* A Z80's instruction, or the rest of one stopped in an earlier slice.
   Returns the cycles it ran in this slice; *stopped is set when it stops
   inside the instruction again. */
static int z80_run(Board *b, int c, int *stopped)
{
    b->icount_start = b->icount;
    b->access = 0;
    b->abort_t = -1;
    *stopped = 0;
    if (b->part[c].active) {
        b->part[c].active = 0;
        b->z80[c] = b->part[c].start;
        b->replay = b->part[c].done;
        b->t0 = b->part[c].t;
    } else {
        if (b->step_hook) b->step_hook(b, c);
        b->part[c].start = b->z80[c];
        b->part[c].irq = -1;
        b->replay = 0;
        b->t0 = 0;
    }
    if (setjmp(b->stop)) {
        *stopped = 1;
        return b->part[c].t - b->t0;
    }
    int cycles = z80_step(&b->z80[c]) - b->t0;
    b->t0 = 0;
    /* the line change held while it was stopped */
    if (b->part[c].irq >= 0) b->z80[c].irq_line = b->part[c].irq;
    b->part[c].irq = -1;
    return cycles;
}

/* One of MAME's timeslices: every CPU runs to the target in turn; one that
   stops short (its slice aborted, or a part cycle left) brings the target
   back to its time. */
static void timeslice(Board *b)
{
    Atime expire = next_expire(b);
    while (at_lt(b->base, expire)) {
        Atime target = at_add_as(b->base, QUANTUM_AS);
        if (at_lt(expire, target)) target = expire;
        b->target = target;
        for (int c = 0; c < BOARD_CPUS; c++) {
            if (b->target.sec < b->local[c].sec) continue;
            int64_t delta = at_diff(b->target, b->local[c]);
            if (delta < apc[c]) continue;
            /* MAME divides by the cycle's attoseconds cut to 32 bits */
            int shift = 0;
            while ((apc[c] >> shift) >= ((int64_t)1 << 32)) shift++;
            int64_t ran = (delta >> shift) / (apc[c] >> shift);
            b->executing = c;
            b->run_cycles = ran;
            b->icount = ran;
            b->abort_slice = 0;
            while (b->icount > 0) {
                int stopped;
                b->icount -= z80_run(b, c, &stopped);
                if (stopped) break;
                if (b->abort_slice) { /* MAME: the cycles left are taken back */
                    b->run_cycles -= b->icount;
                    b->icount = 0;
                }
            }
            ran = b->run_cycles - b->icount;
            b->executing = -1;
            b->total[c] += ran;
            b->local[c] = at_mul_add(b->local[c], ran, apc[c]);
            if (at_lt(b->local[c], b->target)) b->target = at_max(b->local[c], b->base);
        }
        b->base = b->target;
        expire = next_expire(b);
    }
    run_timers(b);
}

void board_run_frame(Board *b)
{
    b->frame_done = 0;
    while (!b->frame_done) timeslice(b);
    b->frame++;
}
