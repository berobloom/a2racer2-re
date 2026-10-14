#include "maudio.h"

#include <SDL.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VOICES 6 /* voice 0 is the music */
#define RATE 22050

typedef struct {
    char name[32];
    int16_t *pcm; /* stereo, RATE */
    int frames;
} clip;

typedef struct {
    const clip *c;
    int pos;
    int loop;
    float gain;
} voice;

struct m_audio {
    const m_assets *a;
    SDL_AudioDeviceID dev;
    clip clips[48];
    int nclips;
    voice v[VOICES];
};

/* DirectSound volume (hundredths of a dB, <= 0) from the menu's 1..100 */
static float gain_of(int volume)
{
    if (volume <= 0)
        return 0.0f;
    int cb = volume * 30 - 3100; /* 0x40a150 */
    return powf(10.0f, (float)cb / 2000.0f);
}

static const clip *load(m_audio *au, const char *name)
{
    for (int i = 0; i < au->nclips; i++)
        if (SDL_strcasecmp(au->clips[i].name, name) == 0)
            return &au->clips[i];
    if (au->nclips >= (int)(sizeof au->clips / sizeof au->clips[0]))
        return NULL;
    uint32_t size = 0;
    const uint8_t *data = m_file(au->a, name, &size);
    if (!data) {
        /* Mrcs.img spells some names differently (CLICK3.WAV, versnelbak.wav) */
        long n = (long)au->a->mrcs.count;
        for (long i = 0; i < n && !data; i++)
            if (SDL_strcasecmp(au->a->mrcs.entries[i].name, name) == 0)
                data = a2_archive_data(&au->a->mrcs, (size_t)i, &size);
    }
    if (!data)
        return NULL;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV_RW(SDL_RWFromConstMem(data, (int)size), 1, &spec, &buf, &len))
        return NULL;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, RATE) < 0) {
        SDL_FreeWAV(buf);
        return NULL;
    }
    cvt.len = (int)len;
    cvt.buf = malloc((size_t)len * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
    if (!cvt.buf) {
        SDL_FreeWAV(buf);
        return NULL;
    }
    memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    if (cvt.needed)
        SDL_ConvertAudio(&cvt);
    else
        cvt.len_cvt = cvt.len; /* already 16-bit stereo 22050: SDL leaves len_cvt unset */
    clip *c = &au->clips[au->nclips++];
    snprintf(c->name, sizeof c->name, "%s", name);
    c->pcm = (int16_t *)cvt.buf;
    c->frames = cvt.len_cvt / 4;
    return c;
}

static void mix(void *ud, Uint8 *stream, int len)
{
    m_audio *au = ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        for (int k = 0; k < VOICES; k++) {
            voice *v = &au->v[k];
            if (!v->c)
                continue;
            if (v->pos >= v->c->frames) {
                if (!v->loop) {
                    v->c = NULL;
                    continue;
                }
                v->pos = 0;
            }
            l += v->c->pcm[v->pos * 2] * v->gain;
            r += v->c->pcm[v->pos * 2 + 1] * v->gain;
            v->pos++;
        }
        out[f * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[f * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    /* debug: A2_MENU_AUDIO_DUMP=<file> appends the mix (s16 stereo 22050) */
    static FILE *dump;
    static int tried;
    if (!tried) {
        tried = 1;
        const char *p = getenv("A2_MENU_AUDIO_DUMP");
        if (p)
            dump = fopen(p, "wb");
    }
    if (dump)
        fwrite(stream, 1, (size_t)len, dump), fflush(dump);
}

m_audio *m_audio_open(const m_assets *a)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return NULL;
    m_audio *au = calloc(1, sizeof *au);
    if (!au)
        return NULL;
    au->a = a;
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    want.userdata = au;
    au->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!au->dev) {
        free(au);
        return NULL;
    }
    SDL_PauseAudioDevice(au->dev, 0);
    return au;
}

void m_audio_close(m_audio *au)
{
    if (!au)
        return;
    SDL_CloseAudioDevice(au->dev);
    for (int i = 0; i < au->nclips; i++)
        free(au->clips[i].pcm);
    free(au);
}

void m_audio_sfx(m_audio *au, const char *name, int volume)
{
    if (!au || volume <= 0)
        return;
    const clip *c = load(au, name);
    if (!c)
        return;
    SDL_LockAudioDevice(au->dev);
    int k = 1;
    for (int i = 1; i < VOICES; i++)
        if (!au->v[i].c) {
            k = i;
            break;
        }
    au->v[k] = (voice){c, 0, 0, gain_of(volume)};
    SDL_UnlockAudioDevice(au->dev);
}

void m_audio_music(m_audio *au, const char *name, int volume)
{
    if (!au)
        return;
    const clip *c = name && volume > 0 ? load(au, name) : NULL;
    SDL_LockAudioDevice(au->dev);
    if (!c || au->v[0].c != c)
        au->v[0] = (voice){c, 0, 1, gain_of(volume)};
    else
        au->v[0].gain = gain_of(volume);
    SDL_UnlockAudioDevice(au->dev);
}

void m_audio_music_volume(m_audio *au, int volume)
{
    if (!au)
        return;
    SDL_LockAudioDevice(au->dev);
    au->v[0].gain = gain_of(volume);
    SDL_UnlockAudioDevice(au->dev);
}

int m_audio_clip_ms(m_audio *au, const char *name)
{
    const clip *c = au ? load(au, name) : NULL;
    return c ? (int)((long)c->frames * 1000 / RATE) : 0;
}

void m_audio_stop_sfx(m_audio *au)
{
    if (!au)
        return;
    SDL_LockAudioDevice(au->dev);
    for (int i = 1; i < VOICES; i++)
        au->v[i].c = NULL;
    SDL_UnlockAudioDevice(au->dev);
}
