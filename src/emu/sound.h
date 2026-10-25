/*
 * Star Force's sound, as MAME outputs it (measured: probes writing the
 * chips with both CPUs parked, MAME's recording of the output): three
 * SN76489As at 2 MHz and an 8-bit DAC, summed into one channel.
 *
 * - An SN76489A: three tone channels and a noise channel. A byte with bit 7
 *   latches a register (bits 4-6: tone 0 period, volume, tone 1, ..., noise
 *   control, noise volume) and sets its low 4 bits; a byte without, a tone
 *   period's high 6 bits (or the latched volume or noise control). A tone
 *   flips every period (0 counts as 1024) ticks of 2 MHz / 16; the noise
 *   shifts its 15-bit register at half the rate of a period of 16, 32, 64
 *   ticks or tone 2's, white (taps 0 and 1) or periodic. A channel gives 0
 *   or 4095 x 10^(-volume / 10) (2 dB steps; 15 silent). All loud at
 *   power-on.
 * - The DAC: each zero count of the CTC's channel 2 puts the next of the
 *   first 16 bytes of the waveform PROM (07b.bin) on it; it gives (v / 128
 *   - 1) x 1638, times the low nibble of the sub's $D000 / 15 (15 at
 *   power-on, where it holds 0).
 * Rendered at 48 kHz, each sample the mean of the chips' ticks in it; the
 * sum clipped to 16 bits.
 */
#ifndef STARFORCE_SOUND_H
#define STARFORCE_SOUND_H

#include <stdint.h>

#define SOUND_RATE 48000
#define SOUND_TICK_HZ 125000 /* 2 MHz / 16 */

typedef struct {
    uint16_t period[3];
    uint8_t vol[4];
    uint8_t noise;      /* the noise control: bit 2 white, bits 0-1 rate */
    uint8_t latch;      /* the register latched (0-7) */
    uint16_t count[4];  /* ticks to the next flip */
    uint8_t out[4];     /* the channels' outputs (noise: its register's bit 0) */
    uint8_t noise_flip; /* the noise's half period */
    uint16_t lfsr;
} Sn76489;

typedef struct {
    Sn76489 sn[3];
    const uint8_t *wave; /* 07b.bin (NULL: the DAC holds 0) */
    uint8_t dac, dac_vol, step;
    int64_t tick;        /* the next tick to run, from power-on */
    double sum;          /* the current sample's ticks so far */
    int sum_n;
} Sound;

void sound_init(Sound *s, const uint8_t *wave);
/* chip 0-2: an SN76489A's byte; 3: $D000 (the DAC's volume); 5: a DAC step */
void sound_write(Sound *s, int chip, uint8_t v);
/* Run the chips to tick `to`, appending the finished 48 kHz samples to out
   (at most max); returns how many. */
int sound_run(Sound *s, int64_t to, int16_t *out, int max);

#endif
