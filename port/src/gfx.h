/* Thin OpenGL helpers for the viewer.
 *
 * The original submits pre-transformed, pre-lit D3DTLVERTEX triangle lists to
 * IDirect3DDevice2::DrawIndexedPrimitive.  Here we let GL do the transform and
 * keep the rest of the shape the same: interleaved vertices + index arrays,
 * one texture bound per batch.
 */
#ifndef A2_GFX_H
#define A2_GFX_H

#include "image.h"

#include <stdint.h>

unsigned a2_gfx_texture_from_image(const a2_image *im, int clamp);
/* Same, but point-sampled.  The HUD art is blitted by the original, so its
 * one-pixel glyph outlines and bracket edges have to stay one pixel wide
 * however far a piece is stretched. */
unsigned a2_gfx_texture_from_image_nearest(const a2_image *im, int clamp);
/* Neutral grey checkerboard, used when a texture is missing. */
unsigned a2_gfx_placeholder_texture(void);
void a2_gfx_delete_texture(unsigned tex);

/* Game.ini [GFX OPTIONS] gamma ("0=light, 1 = dark", shipped 1).  winlib's
 * colour conversion to the 16-bit screen format (0x468230, and the flag's
 * second reader at 0x468436) squares each channel first when it is set --
 * c = c*c >> 8 -- and every texture and sprite loader converts through it
 * (Rcpcx.cpp, Sprite.cpp).  Set it before any texture is loaded. */
void a2_gfx_set_gamma(int dark);
/* Apply it in place to n RGBA pixels (alpha untouched); no-op when light. */
void a2_gfx_gamma_rgba(uint8_t *rgba, size_t n);

#endif /* A2_GFX_H */
