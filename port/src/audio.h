/* Sound effects, after snd_main.cpp.
 *
 * The original plays everything through DirectSound buffers, addressed by an
 * index into its 87-entry sample table (0x48BAF0) and controlled in
 * DirectSound units: volume in hundredths of a dB (0 loudest, -10000 silent),
 * pan -10000..+10000, and a playback frequency in Hz.  This module keeps those
 * units so the game's own numbers can be used as they are, and mixes in
 * software on an SDL audio device.
 *
 * Three kinds of sound:
 *   - one-shots and loops started by sample index (the original's 0x4286a0 and
 *     0x428700), which play at -500 - SFXVOLUME;
 *   - voices the caller keeps adjusting (the player's engine, the slip loop);
 *   - positional object sounds on 20 slots (0x428bc0), one per vehicle in
 *     earshot, with the volume taken from the distance and the pan from the
 *     angle.
 * See "How the race sounds" in research/discoveries.md.
 */
#ifndef A2_AUDIO_H
#define A2_AUDIO_H

#include "archive.h"

typedef struct a2_audio a2_audio;

/* A sound-emitting vehicle, for the positional slots.  `key` must be unique
 * and stable for as long as the vehicle exists. */
typedef struct {
    int key;
    float x, z;
    int sample;       /* index into the sample table */
    const char *anim; /* the vehicle's anim name, for looking up its sample */
    int once;         /* play once instead of looping (a prop's knock, 0x428bc0
                       * passes loop 0); the slot frees itself when it ends */
} a2_sound_src;

/* Open the device and keep `rcs` for loading samples on demand.
 * `sfx_volume` is Raser.ini [SOUND] SFXVOLUME (0 = sound off, as in the
 * original).  `positional` is Game.ini 3dsound.  NULL when there is no audio. */
a2_audio *a2_audio_open(const a2_archive *rcs, int sfx_volume, int positional);
void a2_audio_close(a2_audio *a);

/* Start sample `index` once (loop = 0) or looped, at the original's fixed level.
 * Returns a voice handle, or -1. */
int a2_audio_play(a2_audio *a, int index, int loop);
int a2_audio_playing(const a2_audio *a, int voice);
void a2_audio_stop(a2_audio *a, int voice);
/* Adjust a playing voice in DirectSound units.  `freq` 0 leaves it alone. */
void a2_audio_set(a2_audio *a, int voice, int freq_hz, int volume_cb, int pan);
void a2_audio_stop_all(a2_audio *a);
/* SFXVOLUME, for callers that build their own levels from it. */
int a2_audio_sfx_volume(const a2_audio *a);

/* Feed this frame's sound sources.  The listener is at (lx, lz), facing along
 * (fx, fz).  Sources within 2500 units take a free slot of 20 or keep the one
 * they have; slots whose source has gone out of range or out of the list are
 * stopped. */
void a2_audio_objects(a2_audio *a, const a2_sound_src *src, int count, float lx, float lz,
                      float fx, float fz);
/* Is the source with this key sounding in a slot?  0 once a one-shot has
 * ended, when it was out of range or when every slot was busy -- the moments
 * the original clears an object's pending sample (0x428b85, 0x428b43). */
int a2_audio_object_playing(const a2_audio *a, int key);

#endif /* A2_AUDIO_H */
