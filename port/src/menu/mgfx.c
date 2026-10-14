#include "mgfx.h"

#include <string.h>

void m_fill(m_canvas *c, const uint8_t *background)
{
    if (background)
        memcpy(c->px, background, sizeof c->px);
    else
        memset(c->px, 0, sizeof c->px);
}

void m_blit_part(m_canvas *c, const m_sprite *s, int sx, int sy, int sw, int sh, int x, int y)
{
    for (int j = 0; j < sh; j++) {
        int dy = y + j, py = sy + j;
        if (dy < 0 || dy >= M_H || py < 0 || py >= s->h)
            continue;
        for (int i = 0; i < sw; i++) {
            int dx = x + i, px = sx + i;
            if (dx < 0 || dx >= M_W || px < 0 || px >= s->w)
                continue;
            uint8_t v = s->px[py * s->w + px];
            if (v != M_KEY)
                c->px[dy * M_W + dx] = v;
        }
    }
}

void m_blit_stretch(m_canvas *c, const m_sprite *s, int x1, int y1, int x2, int y2)
{
    int dw = x2 - x1, dh = y2 - y1;
    if (dw <= 0 || dh <= 0)
        return;
    /* 16.16 steps, truncated: matches the rows the original repeats (the
     * options slider's fill, 18 rows onto 20, repeats rows 0 and 9 where an
     * exact j * 18 / 20 would repeat 0 and 10) */
    uint32_t sx = ((uint32_t)s->w << 16) / (uint32_t)dw, sy = ((uint32_t)s->h << 16) / (uint32_t)dh;
    for (int y = y1 < 0 ? 0 : y1; y < y2 && y < M_H; y++) {
        int py = (int)(((uint32_t)(y - y1) * sy) >> 16);
        for (int x = x1 < 0 ? 0 : x1; x < x2 && x < M_W; x++)
            c->px[y * M_W + x] = s->px[py * s->w + (int)(((uint32_t)(x - x1) * sx) >> 16)];
    }
}

void m_blit(m_canvas *c, const m_sprite *s, int x, int y)
{
    m_blit_part(c, s, 0, 0, s->w, s->h, x, y);
}

int m_text_width(const m_font *f, const char *s) { return (int)strlen(s) * f->w; }

/* Text lands on even x: every string measured in captures of the original
 * (Kwalificatie at 278 for a computed 279, the car name at 436 for 437, the
 * button labels) sits at its computed x rounded down to even, while sprites
 * keep odd positions -- LIKELY a word-aligned text blit. */
int m_text(m_canvas *c, const m_font *f, int x, int y, const char *s)
{
    return m_text_ink(c, f, -1, x, y, s);
}

int m_text_ink(m_canvas *c, const m_font *f, int ink, int x, int y, const char *s)
{
    x &= ~1;
    int x0 = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, x += f->w) {
        const uint8_t *g = f->glyphs + (size_t)*p * f->w * f->h;
        for (int j = 0; j < f->h; j++) {
            int dy = y + j;
            if (dy < 0 || dy >= M_H)
                continue;
            for (int i = 0; i < f->w; i++) {
                int dx = x + i;
                uint8_t v = g[j * f->w + i];
                if (v != M_KEY && dx >= 0 && dx < M_W)
                    c->px[dy * M_W + dx] = (ink >= 0 && v == f->ink) ? (uint8_t)ink : v;
            }
        }
    }
    return x - x0;
}

void m_rect(m_canvas *c, int x, int y, int w, int h, uint8_t index)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < M_W && j >= 0 && j < M_H)
                c->px[j * M_W + i] = index;
}

void m_to_rgba(const m_canvas *c, const uint8_t pal[768], uint8_t *rgba)
{
    for (int i = 0; i < M_W * M_H; i++) {
        const uint8_t *p = pal + c->px[i] * 3;
        rgba[i * 4 + 0] = p[0];
        rgba[i * 4 + 1] = p[1];
        rgba[i * 4 + 2] = p[2];
        rgba[i * 4 + 3] = 255;
    }
}
