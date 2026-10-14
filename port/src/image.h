/* Decoded 32-bit RGBA image, plus PCX and TGA decoders.
 *
 * Both formats appear inside the IMG archives.  PCX entries are 8-bit and
 * carry a 256-colour VGA palette in the last 769 bytes; TGA entries are
 * uncompressed true-colour.  Index 255 / magenta is the colour key the game
 * uses for transparency.
 */
#ifndef A2_IMAGE_H
#define A2_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width, height;
    uint8_t *rgba; /* width*height*4, owned */
} a2_image;

void a2_image_free(a2_image *im);

/* Return 0 on success.  `name` only feeds error messages. */
int a2_pcx_decode(a2_image *im, const uint8_t *data, size_t size, const char *name);
int a2_tga_decode(a2_image *im, const uint8_t *data, size_t size, const char *name);

/* An 8-bit PCX as its raw palette indices, `w * h` bytes (malloc'd), or
 * NULL.  The palette is a2_pcx_palette's. */
uint8_t *a2_pcx_decode_indexed(const uint8_t *data, size_t size, const char *name, int *w,
                               int *h);

/* Decode whichever of the two the bytes look like. */
int a2_image_decode(a2_image *im, const uint8_t *data, size_t size, const char *name);

/* Extract just the 256-entry palette from an 8-bit PCX (as 256*3 RGB bytes).
 * The game stores per-track palettes this way (e00_pal.pcx is a 16x16 carrier). */
int a2_pcx_palette(uint8_t out[768], const uint8_t *data, size_t size);

/* Apply the game's colour key.  The key is (255, 0, 127) -- a rose pink, not
 * magenta: it is the dominant colour in 41 of the shipped textures and appears
 * in 54 of 106, whereas pure magenta appears in exactly one. */
void a2_image_key_transparent(a2_image *im);

/* Bleed neighbouring colour into keyed texels so bilinear filtering does not
 * leave a pink fringe around cut-outs. */
void a2_image_bleed_keyed(a2_image *im, int passes);

#endif /* A2_IMAGE_H */
