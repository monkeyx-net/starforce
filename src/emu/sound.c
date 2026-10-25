#include "emu/sound.h"

#include <math.h>
#include <string.h>

#define AMP_MAX 4095.0
#define DAC_AMP 1638.0

static double amp[16];

static void sn_reset(Sn76489 *c)
{
    memset(c, 0, sizeof *c);
    c->lfsr = 0x4000;
    for (int i = 0; i < 4; i++) c->count[i] = 1;
}

void sound_init(Sound *s, const uint8_t *wave)
{
    memset(s, 0, sizeof *s);
    for (int v = 0; v < 15; v++) amp[v] = AMP_MAX * pow(10.0, -v / 10.0);
    amp[15] = 0;
    for (int i = 0; i < 3; i++) sn_reset(&s->sn[i]);
    s->wave = wave;
    s->dac_vol = 15;
}

static void sn_write(Sn76489 *c, uint8_t v)
{
    if (v & 0x80) c->latch = v >> 4 & 7;
    int r = c->latch;
    if (r & 1) {
        c->vol[r >> 1] = v & 0x0F;
    } else if (r == 6) {
        c->noise = v & 7;
        c->lfsr = 0x4000;
    } else if (v & 0x80) {
        c->period[r >> 1] = (uint16_t)((c->period[r >> 1] & 0x3F0) | (v & 0x0F));
    } else {
        c->period[r >> 1] = (uint16_t)((c->period[r >> 1] & 0x0F) | (v & 0x3F) << 4);
    }
}

void sound_write(Sound *s, int chip, uint8_t v)
{
    if (chip < 3) sn_write(&s->sn[chip], v);
    else if (chip == 3) s->dac_vol = v & 0x0F;
    else if (chip == 5) {
        s->dac = s->wave ? s->wave[s->step] : 0;
        s->step = (uint8_t)((s->step + 1) & 15);
    }
}

/* one tick of 2 MHz / 16 */
static double sn_tick(Sn76489 *c)
{
    for (int i = 0; i < 3; i++)
        if (--c->count[i] == 0) {
            c->count[i] = c->period[i] ? c->period[i] : 1024;
            c->out[i] ^= 1;
        }
    if (--c->count[3] == 0) {
        int rate = c->noise & 3;
        c->count[3] = rate == 3 ? (c->period[2] ? c->period[2] : 1024) : (uint16_t)(16 << rate);
        c->noise_flip ^= 1;
        if (c->noise_flip) {
            int fb = c->noise & 4 ? ((c->lfsr ^ c->lfsr >> 1) & 1) : (c->lfsr & 1);
            c->lfsr = (uint16_t)(c->lfsr >> 1 | fb << 14);
            c->out[3] = c->lfsr & 1;
        }
    }
    double v = 0;
    for (int i = 0; i < 4; i++)
        if (c->out[i]) v += amp[c->vol[i]];
    return v;
}

int sound_run(Sound *s, int64_t to, int16_t *out, int max)
{
    int n = 0;
    for (; s->tick < to; s->tick++) {
        double v = (s->dac / 128.0 - 1.0) * DAC_AMP * s->dac_vol / 15.0;
        for (int i = 0; i < 3; i++) v += sn_tick(&s->sn[i]);
        s->sum += v;
        s->sum_n++;
        /* the sample this tick ends: ticks t to t + 1 cover samples t x 48 / 125 on */
        if ((s->tick + 1) * SOUND_RATE / SOUND_TICK_HZ != s->tick * SOUND_RATE / SOUND_TICK_HZ) {
            double m = s->sum / s->sum_n;
            if (n < max) out[n++] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : lrint(m));
            s->sum = 0;
            s->sum_n = 0;
        }
    }
    return n;
}
