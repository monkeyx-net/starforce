#include "sim/sim.h"

#include <string.h>

#include "sim/progs.h"

/* ---- time: the board's (MAME's), in attoseconds ------------------------------------- */

#define AS_PER_SECOND 1000000000000000000LL
#define MAIN_APC 250000000000LL         /* attoseconds a cycle at 4 MHz */
#define SUB_APC 500000000000LL          /* at 2 MHz */
#define QUANTUM_AS (AS_PER_SECOND / 60) /* MAME's slices: 1/60 s */
#define PIXEL_AS 166666666666LL         /* 6 MHz (MAME's, truncated) */
#define LINE_AS (PIXEL_AS * 384)
#define LINES 264
#define VBLANK_LINE 240
#define FRAME_AS (LINE_AS * LINES)
#define SOUND_TIMER_AS 20000000000000000LL /* MAME's sound update, 50 Hz */
#define TICK_AS (AS_PER_SECOND / SOUND_TICK_HZ)

typedef SimTime Atime;

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
static int64_t at_diff(Atime a, Atime b) { return (a.sec - b.sec) * AS_PER_SECOND + (a.as - b.as); }
static const Atime NEVER = { INT64_MAX / 2, 0 };

/* n periods of p attoseconds from t, without overflowing */
static Atime at_mul_add(Atime t, int64_t n, int64_t p)
{
    int64_t per_sec = AS_PER_SECOND / p;
    if (per_sec < 1) per_sec = 1;
    while (n > per_sec) {
        t = at_add_as(t, per_sec * p);
        n -= per_sec;
    }
    return at_add_as(t, n * p);
}

enum { T_VBLANK, T_LINE0, T_VBLANK_END, T_SOUND, T_CTC0, T_SYNC0 = T_CTC0 + 4 };
enum { E_VBLANK, E_LINE0, E_VBLANK_END, E_SOUND, E_CTC, E_IRQ_LINES, E_LATCH };

static void (*const runs[SIM_CPUS])(Cpu *) = { main_run, sub_run };
static const int64_t apc[SIM_CPUS] = { MAIN_APC, SUB_APC };

/* ---- timers ------------------------------------------------------------------------ */

static Atime next_due(const Sim *s)
{
    Atime t = NEVER;
    for (int i = 0; i < SIM_TIMERS; i++)
        if (s->timers[i].active && at_lt(s->timers[i].at, t)) t = s->timers[i].at;
    return t;
}

/* The time now: the running CPU's at the access under way; in a timer's
   event, its time; else the base */
static Atime now(const Sim *s)
{
    if (s->in_event) return s->event_time;
    int c = s->running;
    if (c < 0) return s->base;
    return at_mul_add(s->local[c], s->cpu[c].t_access - s->done[c], apc[c]);
}

/* A timer; one that becomes the first due while a CPU runs ends its slice
   after its instruction (MAME's abort of the timeslice), and the
   instruction's accesses after the one under way wait for the next slice */
static void timer_set(Sim *s, int i, Atime at, int64_t period, int what, int param)
{
    s->timers[i].active = 0;
    if (s->running >= 0 && at_lt(at, next_due(s)) && !s->aborted) {
        Cpu *z = &s->cpu[s->running];
        s->aborted = 1;
        z->budget = z->t;
        if (z->partial && z->t_access + 1 < z->limit) z->limit = z->t_access + 1;
    }
    s->timers[i] = (SimTimer){ at, period, ++s->seq, what, param, 1 };
}

/* MAME's synchronize(): what to do once the other CPU has caught up */
static void sync_now(Sim *s, int what, int param)
{
    for (int i = T_SYNC0; i < SIM_TIMERS; i++)
        if (!s->timers[i].active) {
            timer_set(s, i, now(s), 0, what, param);
            return;
        }
}

/* ---- the sub's daisy chain: the PIO's port A, then the CTC's channels -------------- */

static void irq_lines(Sim *s)
{
    s->cpu[SIM_MAIN].irq_line = (uint8_t)s->irq_main;
    s->cpu[SIM_SUB].irq_line = (uint8_t)s->irq_sub;
}

static int daisy_request(const Sim *s)
{
    if (s->pio.ius) return 0;
    if (s->pio.ip && s->pio.ie) return 1;
    for (int i = 0; i < 4; i++) {
        if (s->ctc[i].ius) return 0;
        if (s->ctc[i].ip && (s->ctc[i].control & 0x80)) return 1;
    }
    return 0;
}

static void update_sub_irq(Sim *s)
{
    int state = daisy_request(s);
    if (state != s->irq_sub) {
        s->irq_sub = state;
        sync_now(s, E_IRQ_LINES, 0);
    }
}

/* the acknowledge: the first device asking gives its vector and is under
   service until RETI */
static uint8_t daisy_ack(Sim *s)
{
    uint8_t v = 0xFF;
    if (!s->pio.ius && s->pio.ip && s->pio.ie) {
        s->pio.ip = 0;
        s->pio.ius = 1;
        v = s->pio.vector;
    } else {
        for (int i = 0; i < 4 && !s->pio.ius; i++) {
            SimCtc *ch = &s->ctc[i];
            if (ch->ius) break;
            if (ch->ip && (ch->control & 0x80)) {
                ch->ip = 0;
                ch->ius = 1;
                v = (uint8_t)(s->ctc_vector | i << 1);
                break;
            }
        }
    }
    update_sub_irq(s);
    return v;
}

static void reti(void *ctx)
{
    Sim *s = ctx;
    if (s->pio.ius) {
        s->pio.ius = 0;
    } else {
        for (int i = 0; i < 4; i++)
            if (s->ctc[i].ius) {
                s->ctc[i].ius = 0;
                break;
            }
    }
    update_sub_irq(s);
}

/* ---- the CTC ------------------------------------------------------------------------- */

static void sound_event(Sim *s, int chip, uint8_t value)
{
    if (s->sound_event_count == SIM_SOUND_EVENTS) return;
    s->sound_events[s->sound_event_count].at = at_diff(now(s), s->frame_start);
    s->sound_events[s->sound_event_count].chip = (uint8_t)chip;
    s->sound_events[s->sound_event_count].value = value;
    s->sound_event_count++;
}

static int64_t ctc_period(const SimCtc *ch)
{
    int prescale = ch->control & 0x20 ? 256 : 16;
    int tc = ch->tc ? ch->tc : 256;
    return (int64_t)prescale * tc * SUB_APC;
}

static void ctc_trigger(Sim *s, int i);

/* a channel's zero count: its interrupt, its pulse (channel 0's clocks
   channel 1, channel 2's steps the DAC) */
static void ctc_zero(Sim *s, int i)
{
    SimCtc *ch = &s->ctc[i];
    if (ch->control & 0x80) {
        ch->ip = 1;
        update_sub_irq(s);
    }
    if (i == 0) ctc_trigger(s, 1);
    else if (i == 2) sound_event(s, 5, 0);
}

static void ctc_trigger(Sim *s, int i)
{
    SimCtc *ch = &s->ctc[i];
    if (!ch->running || !(ch->control & 0x40)) return;
    if (--ch->down <= 0) {
        ch->down = ch->tc ? ch->tc : 256;
        ctc_zero(s, i);
    }
}

static void ctc_write(Sim *s, int i, uint8_t v)
{
    SimCtc *ch = &s->ctc[i];
    if (ch->want_tc) {
        ch->want_tc = 0;
        ch->tc = v;
        if (!ch->running) {
            ch->running = 1;
            if (ch->control & 0x40) {
                ch->down = v ? v : 256;
            } else {
                int64_t p = ctc_period(ch);
                timer_set(s, T_CTC0 + i, at_add_as(now(s), p), p, E_CTC, i);
            }
        }
        return;
    }
    if (!(v & 1)) {
        if (i == 0) s->ctc_vector = v & 0xF8;
        return;
    }
    ch->control = v;
    if (v & 2) {
        ch->running = 0;
        s->timers[T_CTC0 + i].active = 0;
    }
    if (v & 4) ch->want_tc = 1;
    update_sub_irq(s);
}

/* ---- the PIO and the sound latch ----------------------------------------------------- */

static void pio_control(Sim *s, uint8_t v)
{
    if (s->pio.want_mask) {
        s->pio.want_mask = 0;
    } else if (!(v & 1)) {
        s->pio.vector = v;
    } else if ((v & 0x0F) == 0x07) {
        s->pio.ie = v >> 7;
        s->pio.want_mask = (v & 0x10) != 0;
    } else if ((v & 0x0F) == 0x03) {
        s->pio.ie = v >> 7;
    }
    update_sub_irq(s);
}

/* a latch write reaches the sub once it has caught up: the PIO's strobe
   (the first write since power-on strobes nothing, MAME's) */
static void latch_sync(Sim *s, uint8_t v)
{
    s->latch = v;
    if (s->latch_written) {
        s->pio.ip = 1;
        update_sub_irq(s);
    }
    s->latch_written = 1;
}

static uint8_t io_read(void *ctx, uint8_t port)
{
    Sim *s = ctx;
    return port == 0x00 ? s->latch : 0;
}

static void io_write(void *ctx, uint8_t port, uint8_t v)
{
    Sim *s = ctx;
    if (port < 4) {
        if (port == 1) pio_control(s, v); /* port B's control and data have no part */
    } else if (port >= 8 && port < 12) {
        ctc_write(s, port & 3, v);
    }
}

/* the writes with an effect beyond the memory (game_wr's bus_write) */
static void bus_write(void *ctx, int cpu, uint16_t a, uint8_t v)
{
    Sim *s = ctx;
    if (cpu == GAME_MAIN) {
        if (a == 0xD002) {
            s->irq_main = 0;
            sync_now(s, E_IRQ_LINES, 0);
        } else if (a == 0xD004) {
            sync_now(s, E_LATCH, v);
        }
        return;
    }
    switch (a) {
    case 0x8000: sound_event(s, 0, v); break;
    case 0x9000: sound_event(s, 1, v); break;
    case 0xA000: sound_event(s, 2, v); break;
    case 0xD000: sound_event(s, 3, v); break;
    case 0xE000: sound_event(s, 4, v); break;
    default: break;
    }
}

/* ---- the sound ------------------------------------------------------------------------- */

static int64_t ticks(Atime t) { return t.sec * SOUND_TICK_HZ + t.as / TICK_AS; }

/* the frame's sound, from the writes' times in it, to its end */
static void make_sound(Sim *s, Atime end)
{
    int64_t start = ticks(s->frame_start);
    s->audio_count = 0;
    for (int i = 0; i < s->sound_event_count; i++) {
        s->audio_count += sound_run(&s->chips, start + s->sound_events[i].at / TICK_AS, s->audio + s->audio_count,
                                    SIM_AUDIO_MAX - s->audio_count);
        sound_write(&s->chips, s->sound_events[i].chip, s->sound_events[i].value);
    }
    s->audio_count += sound_run(&s->chips, ticks(end), s->audio + s->audio_count, SIM_AUDIO_MAX - s->audio_count);
    s->sound_event_count = 0;
}

/* ---- the screen's events ---------------------------------------------------------------- */

static void fire(Sim *s, const SimTimer *t)
{
    switch (t->what) {
    case E_VBLANK:
        make_sound(s, t->at);
        s->frame_start = t->at;
        s->frame_done = 1;
        s->irq_main = 1;
        sync_now(s, E_IRQ_LINES, 0);
        timer_set(s, T_VBLANK, at_add_as(s->timers[T_VBLANK].at, FRAME_AS), 0, E_VBLANK, 0);
        break;
    case E_LINE0: timer_set(s, T_LINE0, at_add_as(s->timers[T_LINE0].at, FRAME_AS), 0, E_LINE0, 0); break;
    case E_VBLANK_END:
        timer_set(s, T_VBLANK_END, at_add_as(s->timers[T_VBLANK_END].at, FRAME_AS), 0, E_VBLANK_END, 0);
        break;
    case E_SOUND: break;
    case E_CTC: ctc_zero(s, t->param); break;
    case E_IRQ_LINES: irq_lines(s); break;
    case E_LATCH: latch_sync(s, (uint8_t)t->param); break;
    default: break;
    }
}

/* Timers due by the base time, in order (equal times: in the order set) */
static void run_timers(Sim *s)
{
    for (;;) {
        int due = -1;
        for (int i = 0; i < SIM_TIMERS; i++) {
            const SimTimer *t = &s->timers[i];
            if (!t->active || at_lt(s->base, t->at)) continue;
            if (due < 0 || at_lt(t->at, s->timers[due].at) ||
                (!at_lt(s->timers[due].at, t->at) && t->seq < s->timers[due].seq))
                due = i;
        }
        if (due < 0) return;
        SimTimer *t = &s->timers[due];
        SimTimer fired = *t;
        if (t->period) {
            t->at = at_add_as(t->at, t->period);
            t->seq = ++s->seq;
        } else {
            t->active = 0;
        }
        s->in_event = 1;
        s->event_time = fired.at;
        fire(s, &fired);
        s->in_event = 0;
    }
}

/* ---- the CPUs ---------------------------------------------------------------------------- */

static void fetch(Cpu *z, int64_t n) { z->r = (uint8_t)((z->r & 0x80) | ((z->r + n) & 0x7F)); }

/* The IRQ's acknowledge: the sub's daisy chain gives the vector (an access
   at the acceptance's start, replayed when it is run again) */
static uint8_t ack(Cpu *z)
{
    uint8_t v;
    int k = z->acc;
    if (cpu_access(z, &v, 1)) return v;
    z->t_access = z->t_start;
    v = daisy_ack(z->g->bus);
    if (k < 8) z->rep[k] = v;
    return v;
}

/* An IRQ (main IM 1; the sub IM 2), or the one it stopped inside taken on:
   its acknowledge, pushes and vector reads at the machine cycles of the
   board's, where its slice can end as inside an instruction (cpu_access) */
static void take(Cpu *z)
{
    static const uint8_t offs_im1[2] = { 7, 10 }, offs_im2[5] = { 0, 7, 10, 13, 16 };
    int im2 = z->id == GAME_SUB;
    if (z->stopped) { /* run again: its accesses so far replayed */
        z->stopped = 0;
        z->rep_now = z->rep_n;
        z->rep_n = 0;
    } else {
        z->rep_now = z->rep_n = 0;
        z->part_pc = z->pc;
        z->t_start = z->t;
        memcpy(z->saved, z, offsetof(Cpu, regs_end));
    }
    z->in_int = 1;
    z->partial = 1;
    z->acc = 0;
    z->offs = im2 ? offs_im2 : offs_im1;
    fetch(z, 1);
    z->iff = z->iff2 = 0;
    uint8_t vector = im2 ? ack(z) : 0xFF;
    cpu_push(z, z->pc);
    z->pc = im2 ? cpu_rd16(z, (uint16_t)(z->i << 8 | vector)) : 0x0038;
    z->in_int = 0;
    z->partial = 0;
    z->halted = 0;
    z->wz = z->pc;
    z->t = z->t_start + (im2 ? 19 : 13);
}

/* A CPU its `ran` cycles on: the cycles it ran (more when its last
   instruction ends past them; less when it ended the slice; to its stop
   when it stopped inside an instruction) */
static int64_t run(Sim *s, int c, int64_t ran)
{
    Cpu *z = &s->cpu[c];
    jmp_buf jb;
    int64_t from = s->done[c], end = from + ran;
    s->running = c;
    s->aborted = 0;
    z->jb = &jb;
    z->limit = end;
    if (setjmp(jb)) { /* stopped inside an instruction: the rest in its next slice */
        s->running = -1;
        return z->t_stop - from;
    }
    int64_t at = z->stopped ? z->t_stop : z->t; /* where it is: its stop inside an instruction counts */
    while (at < end && !s->aborted) {
        if (z->stopped && z->in_int) take(z);
        else if (!z->stopped && z->irq_line && z->iff && !z->ei_delay) take(z);
        else if (z->stopped || z->ei_delay) { /* an instruction to finish, or the one after an EI: then the interrupts */
            z->ei_delay = 0;
            z->budget = z->t + 1;
            runs[c](z);
        } else {
            z->budget = end;
            runs[c](z);
        }
        at = z->t;
    }
    s->running = -1;
    return z->t - from;
}

/* One of MAME's timeslices: every CPU runs to the target in turn; one that
   ends short brings the target back to its time */
static void timeslice(Sim *s)
{
    Atime due = next_due(s);
    while (at_lt(s->base, due)) {
        Atime target = at_add_as(s->base, QUANTUM_AS);
        if (at_lt(due, target)) target = due;
        s->target = target;
        for (int c = 0; c < SIM_CPUS; c++) {
            if (s->target.sec < s->local[c].sec) continue;
            int64_t delta = at_diff(s->target, s->local[c]);
            if (delta < apc[c]) continue;
            /* MAME divides by the cycle's attoseconds cut to 32 bits */
            int shift = 0;
            while ((apc[c] >> shift) >= ((int64_t)1 << 32)) shift++;
            int64_t ran = (delta >> shift) / (apc[c] >> shift);
            ran = run(s, c, ran);
            s->done[c] += ran;
            s->local[c] = at_mul_add(s->local[c], ran, apc[c]);
            if (at_lt(s->local[c], s->target)) s->target = at_max(s->local[c], s->base);
        }
        s->base = s->target;
        due = next_due(s);
    }
    run_timers(s);
}

static void reset_cpu(Cpu *z)
{
    Game *g = z->g;
    uint8_t id = z->id;
    memset(z, 0, sizeof *z);
    z->g = g;
    z->id = id;
    /* the registers as the board's Z80 (MAME's) has them: F $40, the index
       registers $FFFF, the others 0 */
    z->f = 0x40;
    z->ix = z->iy = 0xFFFF;
    z->stop_pc = 0xFFFF;
    z->stop_sp = 0xFFFF;
}

void sim_init(Sim *s, const uint8_t *data, uint8_t dsw1, uint8_t dsw2)
{
    memset(s, 0, sizeof *s);
    Game *g = &s->g;
    g->data = data;
    g->dsw1 = dsw1;
    g->dsw2 = dsw2;
    g->bus_write = bus_write;
    g->io_read = io_read;
    g->io_write = io_write;
    g->reti = reti;
    g->bus = s;
    for (int c = 0; c < SIM_CPUS; c++) {
        s->cpu[c].g = g;
        s->cpu[c].id = (uint8_t)c;
        reset_cpu(&s->cpu[c]);
    }
    s->running = -1;
    s->pio.ip = 1; /* pending from power-on (MAME's) */
    sound_init(&s->chips, NULL); /* the DAC's waveform: sim_sound */
    /* the screen starts at the vertical blank */
    timer_set(s, T_VBLANK, (Atime){ 0, FRAME_AS }, 0, E_VBLANK, 0);
    timer_set(s, T_LINE0, (Atime){ 0, (LINES - VBLANK_LINE) * LINE_AS }, 0, E_LINE0, 0);
    timer_set(s, T_VBLANK_END, (Atime){ 0, (LINES - VBLANK_LINE + 16) * LINE_AS }, 0, E_VBLANK_END, 0);
    timer_set(s, T_SOUND, (Atime){ 0, SOUND_TIMER_AS }, SOUND_TIMER_AS, E_SOUND, 0);
}

void sim_sound(Sim *s, const uint8_t *wave) { s->chips.wave = wave; }

void sim_frame(Sim *s, uint8_t p1, uint8_t p2, uint8_t system)
{
    s->g.p1 = p1;
    s->g.p2 = p2;
    s->g.system = system;
    s->frame_done = 0;
    while (!s->frame_done) timeslice(s);
    s->frame++;
}
