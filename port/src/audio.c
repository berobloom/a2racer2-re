#include "audio.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The sample table at 0x48BAF0: 87 entries of 19 bytes, addressed by index.
 * 12-15 (3git/2git/1git/gogit) are named but not in the archive. */
static const char *const SAMPLE_NAME[87] = {
    "werk01", "werk02", "kerkklok", "tick", "tick", "tick", "tick", "tick", "tick", "tick",
    "tick", "flits", "3git", "2git", "1git", "gogit", "pionb1", "pionb3", "paal", "paal",
    "tanken", "juich", "start", "motor", "toeren1", "carsoft", "carsoft", "carmid", "carmid2",
    "carhard", "carhard2", "auto1", "auto2", "auto3", "best_bus", "bus", "vracht", "rammel",
    "rammel2", "sliploop", "psirene1", "toeter", "getset", "go", "vliegt", "trein", "heli",
    "repair", "aan11a", "aan14a", "aanh11", "aanh12", "aanh13", "aanh14", "aan21a", "aan22a",
    "aanh21", "aanh22", "aanh23", "aan31a", "aan31b", "aan32a", "aanh33", "aanh34", "achter1",
    "achter2", "achter3", "achter4", "achter5", "achter6", "achter7", "achter8", "achter9",
    "achter11", "achter12", "achter14", "achter19", "opgev1", "opgev2", "opgev3", "opgev4",
    "opgev9", "schuur2", "checkp", "turbo", "toeren2", NULL,
};
#define SAMPLE_COUNT 87

#define VOICES 64      /* the original asks for BUFFERS=60 */
#define OBJ_SLOTS 20   /* 0x4da1b0 .. 0x4da254 */
#define OBJ_RANGE 2500 /* entries in the distance table 0x4da254 */
#define DS_MIN -10000

typedef struct {
    float *pcm; /* mono, -1..1 */
    int frames;
    int rate;
    int tried;
} sample;

typedef struct {
    int active;
    int sample;
    int loop;
    double pos;
    int freq;
    float gain_l, gain_r;
    unsigned id; /* bumps on reuse so stale handles do nothing */
} voice;

typedef struct {
    int key; /* -1 when free */
    int voice;
    int sample;
    int seen;
    int once;
} obj_slot;

struct a2_audio {
    const a2_archive *rcs;
    SDL_AudioDeviceID dev;
    int dev_rate;
    int sfx;
    int positional;
    sample samples[SAMPLE_COUNT];
    voice voices[VOICES];
    unsigned next_id;
    obj_slot slots[OBJ_SLOTS];
    int dist_cb[OBJ_RANGE];
    unsigned rng;
};

/* Handles carry the voice index in the low byte and the voice's id above it. */
static int handle_of(const a2_audio *a, int v) { return (int)((a->voices[v].id << 8) | (unsigned)v); }
static int voice_of(const a2_audio *a, int h)
{
    if (h < 0)
        return -1;
    int v = h & 0xff;
    if (v >= VOICES || !a->voices[v].active || (a->voices[v].id << 8 >> 8) != ((unsigned)h >> 8))
        return -1;
    return v;
}

/* --------------------------------------------------------------- loading */

static int rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Decode a PCM RIFF/WAVE (8- or 16-bit, mono or stereo) to mono float.  The
 * stereo files (the hard crunches, the siren) are folded down: the original
 * pans each buffer as a whole, so their own stereo image is not used here. */
static int decode_wav(sample *s, const uint8_t *d, uint32_t n)
{
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4))
        return 0;
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    uint32_t data_n = 0;
    for (uint32_t i = 12; i + 8 <= n;) {
        uint32_t sz = rd32(d + i + 4);
        if (!memcmp(d + i, "fmt ", 4) && i + 8 + 16 <= n) {
            fmt = rd16(d + i + 8);
            ch = rd16(d + i + 10);
            rate = (int)rd32(d + i + 12);
            bits = rd16(d + i + 22);
        } else if (!memcmp(d + i, "data", 4)) {
            data = d + i + 8;
            data_n = sz <= n - i - 8 ? sz : n - i - 8;
        }
        i += 8 + sz + (sz & 1);
    }
    if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || !data || rate <= 0)
        return 0;
    int bps = bits / 8 * ch;
    int frames = (int)(data_n / (uint32_t)bps);
    float *pcm = malloc(sizeof(float) * (size_t)(frames ? frames : 1));
    if (!pcm)
        return 0;
    for (int f = 0; f < frames; f++) {
        float acc = 0.0f;
        for (int c = 0; c < ch; c++) {
            const uint8_t *p = data + (size_t)f * bps + (size_t)c * (bits / 8);
            acc += bits == 8 ? (p[0] - 128) / 128.0f : (int16_t)rd16(p) / 32768.0f;
        }
        pcm[f] = acc / (float)ch;
    }
    s->pcm = pcm;
    s->frames = frames;
    s->rate = rate;
    return 1;
}

static sample *get_sample(a2_audio *a, int index)
{
    if (index < 0 || index >= SAMPLE_COUNT || !SAMPLE_NAME[index])
        return NULL;
    sample *s = &a->samples[index];
    if (!s->tried) {
        s->tried = 1;
        char name[32];
        snprintf(name, sizeof name, "%s.wav", SAMPLE_NAME[index]);
        long e = a2_archive_find(a->rcs, name);
        uint32_t size = 0;
        const uint8_t *d = e >= 0 ? a2_archive_data(a->rcs, (size_t)e, &size) : NULL;
        if (!d || !decode_wav(s, d, size))
            fprintf(stderr, "audio: sample %d (%s) not available\n", index, name);
    }
    return s->pcm ? s : NULL;
}

/* ----------------------------------------------------------------- mixing */

/* DirectSound volume and pan to linear gains: a buffer at volume v is
 * attenuated by v/100 dB, and a pan p attenuates the far side by |p|/100 dB. */
static void gains(int vol, int pan, float *l, float *r)
{
    if (vol < DS_MIN)
        vol = DS_MIN;
    if (vol > 0)
        vol = 0;
    float g = vol <= DS_MIN ? 0.0f : powf(10.0f, vol / 2000.0f);
    float gl = pan > 0 ? powf(10.0f, -pan / 2000.0f) : 1.0f;
    float gr = pan < 0 ? powf(10.0f, pan / 2000.0f) : 1.0f;
    *l = g * gl;
    *r = g * gr;
}

static void mix(void *ud, Uint8 *stream, int len)
{
    a2_audio *a = ud;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    for (int v = 0; v < VOICES; v++) {
        voice *vc = &a->voices[v];
        if (!vc->active)
            continue;
        const sample *s = &a->samples[vc->sample];
        double step = (double)vc->freq / a->dev_rate;
        for (int f = 0; f < frames; f++) {
            if (vc->pos >= s->frames) {
                if (!vc->loop) {
                    vc->active = 0;
                    break;
                }
                vc->pos = fmod(vc->pos, (double)s->frames);
            }
            int i0 = (int)vc->pos;
            int i1 = i0 + 1 < s->frames ? i0 + 1 : (vc->loop ? 0 : i0);
            float t = (float)(vc->pos - i0);
            float x = s->pcm[i0] + (s->pcm[i1] - s->pcm[i0]) * t;
            out[2 * f] += x * vc->gain_l;
            out[2 * f + 1] += x * vc->gain_r;
            vc->pos += step;
        }
    }
    for (int i = 0; i < 2 * frames; i++) {
        if (out[i] > 1.0f)
            out[i] = 1.0f;
        else if (out[i] < -1.0f)
            out[i] = -1.0f;
    }
}

/* ------------------------------------------------------------------- API */

a2_audio *a2_audio_open(const a2_archive *rcs, int sfx_volume, int positional)
{
    if (!rcs || sfx_volume <= 0) /* SFXVOLUME 0 turns all sound off (0x4286aa) */
        return NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return NULL;
    }
    a2_audio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    want.userdata = a;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->dev) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        free(a);
        return NULL;
    }
    a->rcs = rcs;
    a->dev_rate = have.freq;
    a->sfx = sfx_volume;
    a->positional = positional;
    a->rng = 0x1234567u;
    for (int i = 0; i < OBJ_SLOTS; i++)
        a->slots[i].key = -1;
    /* The distance table (0x428590): 2500 entries falling exponentially from
     * -100 to -3000, i.e. -exp(ln 100 + i * (ln 3000 - ln 100) / 2500). */
    for (int i = 0; i < OBJ_RANGE; i++) {
        double v = -exp(log(100.0) + i * (log(3000.0) - log(100.0)) * 0.0004);
        a->dist_cb[i] = v > -100 ? -100 : v < -3000 ? -3000 : (int)v;
    }
    SDL_PauseAudioDevice(a->dev, 0);
    fprintf(stderr, "audio: %d Hz, SFXVOLUME %d, %s\n", a->dev_rate, sfx_volume,
            positional ? "positional" : "not positional");
    return a;
}

void a2_audio_close(a2_audio *a)
{
    if (!a)
        return;
    SDL_CloseAudioDevice(a->dev);
    for (int i = 0; i < SAMPLE_COUNT; i++)
        free(a->samples[i].pcm);
    free(a);
}

int a2_audio_sfx_volume(const a2_audio *a) { return a ? a->sfx : 0; }

static int start(a2_audio *a, int index, int loop, int vol, int pan, int freq)
{
    sample *s = get_sample(a, index);
    if (!s)
        return -1;
    SDL_LockAudioDevice(a->dev);
    int v = 0;
    while (v < VOICES && a->voices[v].active)
        v++;
    int h = -1;
    if (v < VOICES) {
        voice *vc = &a->voices[v];
        vc->sample = index;
        vc->loop = loop;
        vc->pos = 0.0;
        vc->freq = freq ? freq : s->rate;
        gains(vol, pan, &vc->gain_l, &vc->gain_r);
        vc->id = ++a->next_id & 0xffffff;
        vc->active = 1;
        h = handle_of(a, v);
    }
    SDL_UnlockAudioDevice(a->dev);
    return h;
}

int a2_audio_play(a2_audio *a, int index, int loop)
{
    if (!a)
        return -1;
    /* 0x4286a0 / 0x428700: every sample at -500 - SFXVOLUME. */
    return start(a, index, loop, -500 - a->sfx, 0, 0);
}

int a2_audio_playing(const a2_audio *a, int h) { return a && voice_of(a, h) >= 0; }

void a2_audio_stop(a2_audio *a, int h)
{
    if (!a)
        return;
    SDL_LockAudioDevice(a->dev);
    int v = voice_of(a, h);
    if (v >= 0)
        a->voices[v].active = 0;
    SDL_UnlockAudioDevice(a->dev);
}

void a2_audio_set(a2_audio *a, int h, int freq, int vol, int pan)
{
    if (!a)
        return;
    SDL_LockAudioDevice(a->dev);
    int v = voice_of(a, h);
    if (v >= 0) {
        if (freq > 0)
            a->voices[v].freq = freq;
        gains(vol, pan, &a->voices[v].gain_l, &a->voices[v].gain_r);
    }
    SDL_UnlockAudioDevice(a->dev);
}

void a2_audio_stop_all(a2_audio *a)
{
    if (!a)
        return;
    SDL_LockAudioDevice(a->dev);
    for (int v = 0; v < VOICES; v++)
        a->voices[v].active = 0;
    SDL_UnlockAudioDevice(a->dev);
    for (int i = 0; i < OBJ_SLOTS; i++)
        a->slots[i].key = -1;
}

/* 0x428bc0: distance |dx| + |dz|; pan from the angle in the listener's frame,
 * times 90 and clamped to -90..90, through the table 0x4d9ed8
 * (111 * (angle + 90) - 9990); level table[distance] - SFXVOLUME - 100. */
void a2_audio_objects(a2_audio *a, const a2_sound_src *src, int count, float lx, float lz,
                      float fx, float fz)
{
    if (!a)
        return;
    /* One-shots that have ended.  Their source is still in this frame's list
     * (the caller drops it only once it sees the sound has stopped), so they
     * must not be started again here, or a knocked prop's sample would
     * repeat for ever. */
    int ended[OBJ_SLOTS], n_ended = 0;
    for (int i = 0; i < OBJ_SLOTS; i++) {
        a->slots[i].seen = 0;
        if (a->slots[i].key >= 0 && a->slots[i].once && !a2_audio_playing(a, a->slots[i].voice)) {
            ended[n_ended++] = a->slots[i].key;
            a->slots[i].key = -1; /* a one-shot that has ended */
        }
    }
    float fl = sqrtf(fx * fx + fz * fz);
    if (fl > 0.0f) {
        fx /= fl;
        fz /= fl;
    }
    for (int k = 0; k < count; k++) {
        const a2_sound_src *s = &src[k];
        int done = 0;
        for (int e = 0; e < n_ended && !done; e++)
            done = ended[e] == s->key;
        if (done)
            continue;
        float dx = s->x - lx, dz = s->z - lz;
        int dist = (int)(fabsf(dx) + fabsf(dz));
        int pan = 0;
        if (a->positional) {
            float d = sqrtf(dx * dx + dz * dz);
            /* sideways component, positive to the listener's right: the
             * original's screen right is (fz, -fx) for forward (fx, fz) */
            float side = d > 0.0f ? (dx * fz - dz * fx) / d : 0.0f;
            int ang = (int)(side * 90.0f);
            ang = ang < -90 ? -90 : ang > 90 ? 90 : ang;
            pan = 111 * (ang + 90) - 9990;
        }
        int slot = -1;
        for (int i = 0; i < OBJ_SLOTS; i++)
            if (a->slots[i].key == s->key)
                slot = i;
        if (dist >= OBJ_RANGE) {
            if (slot >= 0) {
                a2_audio_stop(a, a->slots[slot].voice);
                a->slots[slot].key = -1;
            }
            continue;
        }
        int vol = a->dist_cb[dist] - a->sfx - 100;
        if (slot >= 0 && a->slots[slot].sample != s->sample) { /* e.g. siren on/off */
            a2_audio_stop(a, a->slots[slot].voice);
            a->slots[slot].key = -1;
            slot = -1;
        }
        if (slot < 0) {
            for (int i = 0; i < OBJ_SLOTS && slot < 0; i++)
                if (a->slots[i].key < 0)
                    slot = i;
            if (slot < 0)
                continue; /* all 20 busy, as the original drops it */
            sample *smp = get_sample(a, s->sample);
            if (!smp)
                continue;
            /* 0x4288d3: the buffer's own rate plus rand() % 500, kept in
             * 8000..40000.  A private generator, so the sim's rand() is
             * untouched and headless runs stay reproducible. */
            a->rng = a->rng * 1103515245u + 12345u;
            int freq = smp->rate + (int)((a->rng >> 16) % 500);
            freq = freq < 8000 ? 8000 : freq > 40000 ? 40000 : freq;
            int h = start(a, s->sample, !s->once, vol, pan, freq);
            if (h < 0)
                continue;
            a->slots[slot].key = s->key;
            a->slots[slot].voice = h;
            a->slots[slot].sample = s->sample;
            a->slots[slot].once = s->once;
            if (s->once && getenv("A2_AUDIO_LOG"))
                fprintf(stderr, "audio: one-shot %d (sample %d) started\n", s->key, s->sample);
        } else {
            a2_audio_set(a, a->slots[slot].voice, 0, vol, pan);
        }
        a->slots[slot].seen = 1;
    }
    for (int i = 0; i < OBJ_SLOTS; i++)
        if (a->slots[i].key >= 0 && !a->slots[i].seen) {
            a2_audio_stop(a, a->slots[i].voice);
            a->slots[i].key = -1;
        }
}

int a2_audio_object_playing(const a2_audio *a, int key)
{
    if (!a)
        return 0;
    for (int i = 0; i < OBJ_SLOTS; i++)
        if (a->slots[i].key == key)
            return a2_audio_playing(a, a->slots[i].voice);
    return 0;
}
