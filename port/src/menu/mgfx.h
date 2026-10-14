/* The menu's 640x480 8-bit canvas: the original draws everything into a
 * DirectDraw surface in the menu palette, so the port does the same and only
 * converts to RGB on the way to the screen. */
#ifndef A2_MGFX_H
#define A2_MGFX_H

#include "massets.h"

typedef struct {
    uint8_t px[M_W * M_H];
} m_canvas;

void m_fill(m_canvas *c, const uint8_t *background); /* NULL: index 0 */
/* Top-left at (x, y), clipped, index 0xff transparent. */
void m_blit(m_canvas *c, const m_sprite *s, int x, int y);
/* Part of a sprite: source rect (sx, sy, sw, sh) to (x, y). */
void m_blit_part(m_canvas *c, const m_sprite *s, int sx, int sy, int sw, int sh, int x, int y);
/* The whole sprite stretched onto the rect x1..x2-1, y1..y2-1, nearest
 * neighbour and opaque, as an IDirectDrawSurface::Blt between rects of
 * different sizes (0x4122e0 without DDBLT_KEYSRC). */
void m_blit_stretch(m_canvas *c, const m_sprite *s, int x1, int y1, int x2, int y2);
/* Fixed-cell text; returns the width drawn.  Characters without a glyph
 * advance a cell. */
int m_text(m_canvas *c, const m_font *f, int x, int y, const char *s);
/* The same with the menu's set-colour (0x40f4f0): pixels of the font's own ink
 * index are drawn in `ink` instead (CONFIRMED on the garage's BUDGET, 0x1f,
 * and prices, 0x3a).  ink < 0 draws the font as it is. */
int m_text_ink(m_canvas *c, const m_font *f, int ink, int x, int y, const char *s);
int m_text_width(const m_font *f, const char *s);
void m_rect(m_canvas *c, int x, int y, int w, int h, uint8_t index);
/* To 32-bit RGBA (A = 255) through the palette. */
void m_to_rgba(const m_canvas *c, const uint8_t pal[768], uint8_t *rgba);

#endif /* A2_MGFX_H */
