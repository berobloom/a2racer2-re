/* The menu's sound: WAVs from Mrcs.img by name, one looped music voice and a
 * few effect voices.
 *
 * menu.dat plays effects through 0x40a170 (only when effects are on, at
 * volume * 30 - 3100 hundredths of a dB) and music through 0x40a1c0 (looped,
 * only when music is on, at the music volume).  The menu music follows the
 * next stage: etappe % 7 picks speed, energy, jammin, moody, disco, speed2,
 * moody (table 0x42ef10, played by 0x407500).
 */
#ifndef A2_MAUDIO_H
#define A2_MAUDIO_H

#include "massets.h"

typedef struct m_audio m_audio;

m_audio *m_audio_open(const m_assets *a);
void m_audio_close(m_audio *au);
/* volume 1..100 as the options store it; 0 = off */
void m_audio_sfx(m_audio *au, const char *name, int volume);
/* a clip's length in milliseconds (0 if it is missing) */
int m_audio_clip_ms(m_audio *au, const char *name);
void m_audio_stop_sfx(m_audio *au);
void m_audio_music(m_audio *au, const char *name, int volume); /* NULL stops */
void m_audio_music_volume(m_audio *au, int volume);
/* A video's sound, mixed in at full volume as it is queued: the web build
 * has one audio device, which the menu holds.  Returns 0 if it was not
 * taken. */
int m_audio_stream(m_audio *au, const void *pcm, size_t bytes, int rate, int channels, int bits);
void m_audio_stream_stop(m_audio *au); /* drop what is still queued */

#endif /* A2_MAUDIO_H */
