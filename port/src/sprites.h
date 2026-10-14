/* 2D sprite overlay from RC.IMG (the in-game HUD art).
 *
 * RC.IMG uses the binary IND (u16 count + count x u32 offsets) and each entry is
 * an 8-bit paletted sprite: `u16 width, height, anchor_x, anchor_y;` then
 * width*height indices, with 0xFF transparent.  The palette is external -- the
 * game loads the per-track one named by Raser.ini [PALET].
 *
 * Useful runs in the shipped RC.IMG:
 *   [0] + [32..50]   20 frames of a 49x71 gauge, one per needle position
 *   [1]              the 148x114 speedometer dial (needle baked in at rest)
 *   [11..21]         11 frames of a 91x8 bar
 *   [22..31]         10 frames of a 61x31 icon
 */
#ifndef A2_SPRITES_H
#define A2_SPRITES_H

#include "archive.h"

typedef struct a2_sprites a2_sprites;

/* Load RC.IMG/RC.IND from `datadir`, palettised with a 768-byte RGB table.
 * Returns NULL on failure. */
a2_sprites *a2_sprites_load(const char *datadir, const uint8_t palette[768]);
void a2_sprites_free(a2_sprites *s);

int a2_sprites_count(const a2_sprites *s);
int a2_sprite_size(const a2_sprites *s, int index, int *w, int *h);
/* Sprite `index` as malloc'd RGBA (the transparent index as alpha 0), for
 * stamping into a texture the way the original's txt_main.cpp blits do.
 * The caller frees it.  NULL on failure. */
uint8_t *a2_sprite_rgba(const a2_sprites *s, int index, int *w, int *h);

/* Begin/end a 2D overlay pass over a `width` x `height` screen. */
void a2_sprites_begin(int width, int height);
void a2_sprites_end(void);

/* Draw sprite `index` with its top-left at (x, y), scaled by `scale`. */
void a2_sprite_draw(const a2_sprites *s, int index, float x, float y, float scale);

/* Draw the sub-rectangle (sx, sy, sw, sh) of sprite `index` into the screen
 * rectangle (x, y, dw, dh).  Several HUD sprites are little sheets holding a
 * group of pieces -- 425 carries the top-right brackets and 426 the damage and
 * turbo bar frames, the speed bracket and the "km/h" mark -- and the pieces
 * stretch to whatever width the element needs. */
void a2_sprite_draw_sub(const a2_sprites *s, int index, int sx, int sy, int sw, int sh, float x,
                        float y, float dw, float dh);

#endif /* A2_SPRITES_H */
