/* A Smacker 2 (SMK2) decoder for the menu's videos (intro, win, lose, exit:
 * 320x240, 8-bit palette, 15 fps, one 22050 Hz mono track, 8- or 16-bit).
 *
 * menu.dat plays them through RAD's SMACKW32.DLL; this is a decoder for the
 * documented format: the header's four Huffman "big trees" (MMAP, MCLR, FULL,
 * TYPE) with their three-entry code cache, palette deltas, block types
 * skip / mono / full / fill, and Huffman-DPCM audio.
 */
#ifndef A2_MSMACKER_H
#define A2_MSMACKER_H

#include <stddef.h>
#include <stdint.h>

typedef struct smk smk;

smk *smk_open(const uint8_t *data, size_t size); /* data must outlive it */
void smk_close(smk *s);
int smk_width(const smk *s);
int smk_height(const smk *s);
int smk_frames(const smk *s);
double smk_frame_ms(const smk *s);
/* Audio track 0: rate, channels, bits (8 or 16); 0 rate if none. */
void smk_audio_info(const smk *s, int *rate, int *channels, int *bits);

/* Decode the next frame into the internal buffers.  Returns 0, or -1 at the
 * end or on a damaged frame. */
int smk_next(smk *s);
const uint8_t *smk_pixels(const smk *s); /* width * height indices */
const uint8_t *smk_palette(const smk *s); /* 256 * RGB */
/* This frame's audio for track 0, in the track's own format (u8 or s16le). */
const uint8_t *smk_audio(const smk *s, size_t *bytes);

#endif /* A2_MSMACKER_H */
