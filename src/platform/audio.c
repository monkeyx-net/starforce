#include "platform/audio.h"

#include <SDL.h>
#include <string.h>

#include "assets/assets.h"

#define CROSSFADE_MS 140
#define MAX_VOICES 6       /* tracked (not fading) voices */
#define CHANNELS 16        /* physical voices, fading ones included */
#define MAX_CLIPS 64

typedef struct {
    Sint16 *samples;       /* stereo, device rate */
    Uint32 frames;
    int priority;
    float volume;
    int loop;              /* plays until stopped */
} Clip;

typedef struct {
    int active, fading, id, priority;
    Uint32 pos;
    float gain, fade_step;
    Uint32 started;        /* play order, for "oldest" */
} Voice;

static SDL_AudioDeviceID dev;
static SDL_AudioSpec have;
static Clip clips[MAX_CLIPS];
static int clip_count;
static char sounds_dir[1024]; /* holds sounds/, or "" */
static Voice voices[CHANNELS];
static Uint32 play_counter;

/* The stream: a ring of mono samples at the device rate. */
#define STREAM_SIZE 16384
#define STREAM_MAX_LATENCY 4800 /* drop the oldest beyond 100 ms at 48 kHz */
#define STREAM_PRIME 1600       /* start (and restart after running dry) at ~2 frames queued */
static Sint16 stream[STREAM_SIZE];
static int stream_read, stream_count;
static double stream_phase; /* resampler position between input samples */
static Sint16 stream_last;
static int stream_primed;

static void mix(void *ud, Uint8 *stream_bytes, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream_bytes;
    int frames = len / 4;
    memset(stream_bytes, 0, (size_t)len);
    if (!stream_primed && stream_count >= STREAM_PRIME) stream_primed = 1;
    if (stream_primed && stream_count < frames) stream_primed = 0; /* ran dry: refill first */
    for (int i = 0; stream_primed && i < frames; i++) {
        Sint16 v = stream[stream_read];
        stream_read = (stream_read + 1) % STREAM_SIZE;
        stream_count--;
        out[i * 2] = out[i * 2 + 1] = v;
    }
    for (int c = 0; c < CHANNELS; c++) {
        Voice *v = &voices[c];
        if (!v->active) continue;
        const Clip *clip = &clips[v->id];
        float vol = clip->volume;
        for (int i = 0; i < frames; i++) {
            if (v->pos >= clip->frames && clip->loop) v->pos = 0;
            if (v->pos >= clip->frames || v->gain <= 0) {
                v->active = 0;
                break;
            }
            for (int ch = 0; ch < 2; ch++) {
                int s = out[i * 2 + ch] + (int)(clip->samples[v->pos * 2 + ch] * vol * v->gain);
                out[i * 2 + ch] = (Sint16)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            }
            v->pos++;
            if (v->fading) v->gain -= v->fade_step;
        }
    }
}

int audio_load(const char *file, int priority, float volume)
{
    if (!dev || !sounds_dir[0] || clip_count >= MAX_CLIPS) return -1;
    int id = clip_count;
    char path[1200];
    snprintf(path, sizeof path, "%s/sounds/%s", sounds_dir, file);
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) return -1;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, have.freq) < 0) {
        SDL_FreeWAV(buf);
        return -1;
    }
    cvt.len = (int)len;
    cvt.buf = SDL_malloc((size_t)len * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
    if (!cvt.buf) {
        SDL_FreeWAV(buf);
        return -1;
    }
    memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        return -1;
    }
    clips[id].samples = (Sint16 *)cvt.buf;
    clips[id].frames = (Uint32)(cvt.needed ? cvt.len_cvt : cvt.len) / 4;
    clips[id].priority = priority;
    clips[id].volume = volume;
    clips[id].loop = 0;
    clip_count++;
    return id;
}

int audio_init(const char *argv0)
{
    if (assets_find(argv0, "sounds", sounds_dir, sizeof sounds_dir)) sounds_dir[0] = '\0';
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL audio: %s", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev) {
        SDL_Log("SDL_OpenAudioDevice: %s", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(dev, 0);
    return 1;
}

static void fade_out(Voice *v)
{
    v->fading = 1;
    v->fade_step = v->gain / (have.freq * CROSSFADE_MS / 1000.0f);
}

void audio_stream(const int16_t *samples, int n, int rate)
{
    if (!dev || n <= 0) return;
    double step = (double)rate / have.freq; /* input samples per output sample */
    SDL_LockAudioDevice(dev);
    for (;;) {
        int i = (int)stream_phase;
        if (i >= n) break;
        double t = stream_phase - i;
        Sint16 a = i ? samples[i - 1] : stream_last, b = samples[i];
        Sint16 v = (Sint16)(a + (b - a) * t);
        if (stream_count == STREAM_SIZE || stream_count >= STREAM_MAX_LATENCY) { /* drop the oldest */
            stream_read = (stream_read + 1) % STREAM_SIZE;
            stream_count--;
        }
        stream[(stream_read + stream_count) % STREAM_SIZE] = v;
        stream_count++;
        stream_phase += step;
    }
    stream_phase -= n;
    stream_last = samples[n - 1];
    SDL_UnlockAudioDevice(dev);
}

void audio_play(int id)
{
    if (!dev || id < 0 || id >= clip_count || !clips[id].samples) return;
    int prio = clips[id].priority;
    SDL_LockAudioDevice(dev);
    /* voices of equal or lower priority fade out; higher ones play on underneath */
    int tracked = 0;
    for (int c = 0; c < CHANNELS; c++) {
        Voice *v = &voices[c];
        if (!v->active || v->fading) continue;
        if (v->priority > prio) tracked++;
        else fade_out(v);
    }
    while (tracked >= MAX_VOICES) {
        Voice *oldest = NULL;
        for (int c = 0; c < CHANNELS; c++)
            if (voices[c].active && !voices[c].fading && (!oldest || voices[c].started < oldest->started))
                oldest = &voices[c];
        if (!oldest) break;
        fade_out(oldest);
        tracked--;
    }
    Voice *slot = NULL;
    for (int c = 0; c < CHANNELS && !slot; c++)
        if (!voices[c].active) slot = &voices[c];
    if (!slot) { /* every channel busy: take the quietest fading one */
        for (int c = 0; c < CHANNELS; c++)
            if (voices[c].fading && (!slot || voices[c].gain < slot->gain)) slot = &voices[c];
    }
    if (slot) {
        slot->active = 1;
        slot->fading = 0;
        slot->id = id;
        slot->priority = prio;
        slot->pos = 0;
        slot->gain = 1;
        slot->started = ++play_counter;
    }
    SDL_UnlockAudioDevice(dev);
}

void audio_set_loop(int id, int loop)
{
    if (id >= 0 && id < clip_count) clips[id].loop = loop;
}

void audio_fade_all(void)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    for (int c = 0; c < CHANNELS; c++)
        if (voices[c].active && !voices[c].fading) fade_out(&voices[c]);
    SDL_UnlockAudioDevice(dev);
}

void audio_stop(int id)
{
    if (!dev || id < 0) return;
    SDL_LockAudioDevice(dev);
    for (int c = 0; c < CHANNELS; c++)
        if (voices[c].id == id) voices[c].active = 0;
    SDL_UnlockAudioDevice(dev);
}

void audio_stop_all(void)
{
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    for (int c = 0; c < CHANNELS; c++) voices[c].active = 0;
    SDL_UnlockAudioDevice(dev);
}

void audio_shutdown(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    for (int i = 0; i < clip_count; i++) {
        SDL_free(clips[i].samples);
        clips[i].samples = NULL;
    }
    clip_count = 0;
}
