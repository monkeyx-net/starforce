/*
 * The emulated board against MAME (label mame):
 *   test_emu <starforc.zip|folder> <script> <capture dir> <trace dir>
 *            <trace from> <trace to> <frame>...
 * Runs the scripted game the probes ran (the same PROBE_SCRIPT string, e.g.
 * "700:coin 710: 800:start 810:") from power-on and checks it against
 * MAME:
 *   - MAME's instruction traces of frames <trace from>-<trace to>
 *     (trace_probe.lua, <trace dir>/<cpu>.tr for maincpu and sub): every
 *     instruction at the same cycle with the same registers.
 *   - at each given frame, what capture_probe.lua dumped (capture
 *     dir/frame_<n>.txt) and MAME's snapshot (capture dir/<n>.png): all
 *     RAM, the video RAM and registers, the flip, and the picture pixel for
 *     pixel.
 * MAME's frame callback, where the probes dump, runs at the vertical blank,
 * as the board's frames end. Inputs a MAME probe sets in its frame callback
 * reach the game a frame later. DUMP_DIR=dir writes the board's pictures.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/png.h"
#include "assets/rom.h"
#include "emu/board.h"
#include "gfx/starforce.h"
#include "tests/script.h"

static RomSet rom;
static Board board;
static SfGfx gfx;
static Script script;

static void inputs(Board *b)
{
    script_inputs(&script, (long)b->frame - 1, &b->p1, &b->p2, &b->system);
}

/* ---- traces ---------------------------------------------------------------- */

static const char *const cpu_names[BOARD_CPUS] = { "maincpu", "sub" };

typedef struct {
    int64_t cycles;
    uint16_t reg[8]; /* AF BC DE HL SP IX IY PC */
} Line;

typedef struct {
    Line *l;
    long n, cap;
} Trace;

static Trace mame[BOARD_CPUS], ours[BOARD_CPUS];
static int traced[BOARD_CPUS];
static long trace_from, trace_to;

static void trace_add(Trace *t, const Line *l)
{
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1 << 16;
        t->l = realloc(t->l, (size_t)t->cap * sizeof *t->l);
    }
    t->l[t->n++] = *l;
}

/* MAME's trace: "cycles REG... PC: disassembly" */
static int load_trace(Trace *t, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char text[512];
    while (fgets(text, sizeof text, f)) {
        char *p = text, *end;
        Line l;
        memset(&l, 0, sizeof l);
        l.cycles = strtoll(p, &end, 10);
        if (end == p || *end != ' ') continue;
        p = end;
        int i;
        for (i = 0; i < 8; i++) {
            unsigned long v = strtoul(p, &end, 16);
            if (end == p) break;
            l.reg[i] = (uint16_t)v;
            p = end;
            if (*p == ':') break;
        }
        if (i < 7) continue;
        trace_add(t, &l);
    }
    fclose(f);
    return 0;
}

static void step_hook(Board *b, int c)
{
    if (!traced[c] || b->frame < trace_from || b->frame >= trace_to) return;
    const Z80 *z = &b->z80[c];
    if (z->nmi_pending || (z->irq_line && z->iff1 && !z->ei_delay)) return; /* MAME's trace has no line for it */
    Line l;
    l.cycles = board_total_cycles(b, c);
    uint16_t r[8] = { (uint16_t)(z->a << 8 | z->f), (uint16_t)(z->b << 8 | z->c), (uint16_t)(z->d << 8 | z->e),
                      (uint16_t)(z->h << 8 | z->l), z->sp, (uint16_t)(z->ixh << 8 | z->ixl),
                      (uint16_t)(z->iyh << 8 | z->iyl), z->pc };
    memcpy(l.reg, r, sizeof r);
    trace_add(&ours[c], &l);
}

/* Both traces from MAME's first line on; 0 if equal. */
static int compare_trace(int c)
{
    Trace *a = &mame[c], *b = &ours[c];
    long j = 0;
    while (j < b->n && b->l[j].cycles < a->l[0].cycles) j++;
    long n = a->n < b->n - j ? a->n : b->n - j;
    if (n < a->n * 9 / 10) {
        printf("%s: our trace covers %ld of MAME's %ld lines\n", cpu_names[c], n, a->n);
        return 1;
    }
    for (long i = 0; i < n; i++) {
        const Line *x = &a->l[i], *y = &b->l[j + i];
        if (x->cycles != y->cycles || memcmp(x->reg, y->reg, sizeof x->reg)) {
            printf("%s: instruction %ld of %ld differs: MAME cycle %lld PC %04X, ours cycle %lld PC %04X\n", cpu_names[c], i,
                   n, (long long)x->cycles, x->reg[7], (long long)y->cycles, y->reg[7]);
            for (long k = i > 3 ? i - 3 : 0; k <= i + 1 && k < n; k++) {
                const Line *u = &a->l[k], *v = &b->l[j + k];
                printf("  MAME %lld %04X %04X %04X %04X %04X %04X %04X %04X\n", (long long)u->cycles, u->reg[0], u->reg[1],
                       u->reg[2], u->reg[3], u->reg[4], u->reg[5], u->reg[6], u->reg[7]);
                printf("  ours %lld %04X %04X %04X %04X %04X %04X %04X %04X\n", (long long)v->cycles, v->reg[0], v->reg[1],
                       v->reg[2], v->reg[3], v->reg[4], v->reg[5], v->reg[6], v->reg[7]);
            }
            return 1;
        }
    }
    printf("%s: %ld instructions equal MAME's trace\n", cpu_names[c], n);
    return 0;
}

/* ---- captures -------------------------------------------------------------- */

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int hex_bytes(const char *p, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        int hi = hex(p[2 * i]), lo = hex(p[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

/* frame_<n>.txt against the board; returns the bytes that differ, or -1 */
static long compare_memory(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char text[0x3000 * 2 + 64];
    static const struct {
        const char *name;
        size_t off, len;
        uint16_t addr;
    } parts[] = {
        { "ram", offsetof(Board, ram), 0x1000, 0x8000 },     { "fgvideo", offsetof(Board, fgvideo), 0x400, 0x9000 },
        { "fgcolor", offsetof(Board, fgcolor), 0x400, 0x9400 }, { "sprites", offsetof(Board, sprites), 0x80, 0x9800 },
        { "palette", offsetof(Board, palette), 0x200, 0x9C00 }, { "regs", offsetof(Board, regs), 0x40, 0x9E00 },
        { "bg3", offsetof(Board, bg3), 0x800, 0xA000 },         { "bg2", offsetof(Board, bg2), 0x800, 0xA800 },
        { "bg1", offsetof(Board, bg1), 0x800, 0xB000 },         { "radar", offsetof(Board, radar), 0x400, 0xB800 },
        { "subram", offsetof(Board, subram), 0x400, 0x4000 },
    };
    static uint8_t buf[0x1000];
    int have = 0;
    long diff = 0;
    while (fgets(text, sizeof text, f)) {
        for (size_t k = 0; k < sizeof parts / sizeof parts[0]; k++) {
            size_t n = strlen(parts[k].name);
            if (strncmp(text, parts[k].name, n) || text[n] != ' ') continue;
            if (hex_bytes(text + n + 1, buf, (int)parts[k].len)) continue;
            have++;
            const uint8_t *mine = (const uint8_t *)&board + parts[k].off;
            for (size_t i = 0; i < parts[k].len; i++)
                if (buf[i] != mine[i] && diff++ < 4)
                    printf("  %s $%04X: %02X, MAME %02X\n", parts[k].name, (unsigned)(parts[k].addr + i), mine[i], buf[i]);
        }
        if (!strncmp(text, "flip ", 5)) {
            int v = hex(text[5]) < 0 ? 0 : (hex(text[5]) << 4 | hex(text[6])) & 1;
            if (v != board.flip && diff++ < 4) printf("  flip %d, MAME %d\n", board.flip, v);
        }
    }
    fclose(f);
    if (have != (int)(sizeof parts / sizeof parts[0])) return -1;
    return diff;
}

/* the board's picture against MAME's snapshot; returns the pixels that differ, or -1 */
static long compare_picture(const char *path, long frame)
{
    static uint32_t screen[SF_H][SF_W];
    SfFrame fr;
    sf_frame_from_board(&fr, &board);
    sf_draw(&gfx, &fr, &screen[0][0]);
    const char *dump = getenv("DUMP_DIR");
    if (dump && *dump) {
        static uint8_t rgba[SF_W * SF_H * 4];
        for (int y = 0; y < SF_H; y++)
            for (int x = 0; x < SF_W; x++) {
                uint32_t p = screen[y][x];
                uint8_t *o = rgba + ((size_t)y * SF_W + (size_t)x) * 4;
                o[0] = (uint8_t)(p >> 16), o[1] = (uint8_t)(p >> 8), o[2] = (uint8_t)p, o[3] = 0xFF;
            }
        char out[1100];
        snprintf(out, sizeof out, "%s/board_%ld.png", dump, frame);
        png_write(out, rgba, SF_W, SF_H);
    }
    Image shot;
    const char *err;
    if (png_load(path, &shot, &err)) return -1;
    if (shot.width != SF_W || shot.height != SF_H) {
        image_free(&shot);
        return -1;
    }
    long diff = 0;
    for (int y = 0; y < SF_H; y++)
        for (int x = 0; x < SF_W; x++) {
            const uint8_t *p = shot.rgba + ((size_t)y * SF_W + (size_t)x) * 4;
            uint32_t want = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            if ((screen[y][x] & 0xFFFFFF) != want && diff++ < 4)
                printf("  pixel %d,%d: %06X, MAME %06X\n", x, y, (unsigned)(screen[y][x] & 0xFFFFFF), (unsigned)want);
        }
    image_free(&shot);
    return diff;
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: test_emu <rom> <script> <capture dir> <trace dir> <trace from> <trace to> <frame>...\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (script_parse(&script, argv[2])) {
        fprintf(stderr, "test_emu: bad script\n");
        return 2;
    }
    trace_from = atol(argv[5]);
    trace_to = atol(argv[6]);
    int failures = 0;
    if (trace_to > trace_from)
        for (int c = 0; c < BOARD_CPUS; c++) {
            char path[1100];
            snprintf(path, sizeof path, "%s/%s.tr", argv[4], cpu_names[c]);
            if (!load_trace(&mame[c], path) && mame[c].n) traced[c] = 1;
        }
    sf_gfx_from_rom(&gfx, &rom);
    board_init(&board, &rom);
    board.step_hook = step_hook;
    for (int i = 7; i < argc; i++) {
        long frame = atol(argv[i]);
        while (board.frame < frame) {
            board_run_frame(&board);
            inputs(&board);
        }
        char path[1100];
        snprintf(path, sizeof path, "%s/frame_%ld.txt", argv[3], frame);
        long bytes = compare_memory(path);
        snprintf(path, sizeof path, "%s/%ld.png", argv[3], frame);
        long pixels = compare_picture(path, frame);
        if (bytes < 0 || pixels < 0) {
            printf("frame %ld: no capture\n", frame);
            failures++;
            continue;
        }
        printf("frame %ld: %ld bytes and %ld pixels differ\n", frame, bytes, pixels);
        if (bytes || pixels) failures++;
    }
    while (board.frame < trace_to) {
        board_run_frame(&board);
        inputs(&board);
    }
    for (int c = 0; c < BOARD_CPUS; c++)
        if (traced[c] && compare_trace(c)) failures++;
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("emu: the board equals MAME");
    return 0;
}
