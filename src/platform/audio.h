/*
 * Sound playback through SDL, the mixing model of the Robotron port: sounds
 * overlap; a new one fades out playing voices of equal or lower priority over
 * 140 ms while higher-priority ones play on underneath; at most 6 voices are
 * tracked, the oldest fading out first. The game registers its sounds
 * (WAV files in assets/sounds/) at start; without them or a device it plays nothing.
 */
#ifndef STARFORCE_AUDIO_H
#define STARFORCE_AUDIO_H

#include <stdint.h>

/* Open the device. Returns 1 if sound output is available. */
int audio_init(const char *argv0);
/* Load assets/sounds/<file>; returns a sound id, or -1. */
int audio_load(const char *file, int priority, float volume);
void audio_play(int id);
/* Make a sound loop until it is stopped or faded. */
void audio_set_loop(int id, int loop);
/* Fade out every voice (the crossfade's 140 ms). */
void audio_fade_all(void);
/* Queue mono samples made at `rate` (the emulated sound board's output); they
   play on top of the voices, resampled to the device's rate. */
void audio_stream(const int16_t *samples, int n, int rate);
/* Stop every voice playing this sound at once. */
void audio_stop(int id);
void audio_stop_all(void);
void audio_shutdown(void);

#endif
