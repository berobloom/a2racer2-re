/* menu.dat's art: Mrcs.img (named: backgrounds, fonts, sounds) and
 * MENURC.IMG (binary: 482 sprites), all in one 256-colour palette.
 *
 * - The seven full-screen PCX backgrounds share a byte-identical palette, and
 *   that palette is also MENURC's (see "Sprites" and "Menu fonts" in
 *   reverse-engineering/file-formats.md).
 * - The fonts are fixed-cell bitmaps: u8 present[256], u16 w, h, ink, key,
 *   then 256 glyphs of w*h palette indices, 0xff transparent.
 * - Sprites are u16 w, h, anchor_x, anchor_y + w*h indices, 0xff transparent.
 */
#ifndef A2_MASSETS_H
#define A2_MASSETS_H

#include <stdint.h>

#include "../archive.h"

#define M_W 640
#define M_H 480
#define M_KEY 0xff

typedef struct {
    int w, h, ax, ay;
    const uint8_t *px;
} m_sprite;

typedef struct {
    int w, h;
    int ink;                /* header u16 at 260: the index a set colour replaces */
    const uint8_t *present; /* 256 flags */
    const uint8_t *glyphs;  /* 256 * w * h */
} m_font;

/* The seven fonts, in Mrcs.img order. */
enum {
    M_FONT_CARVED,  /* 10x17 grey, engraved */
    M_FONT_SMALLBT, /* 10x17 red */
    M_FONT_ASCII12, /* 8x12 yellow */
    M_FONT_CYBER,   /* 8x17 grey */
    M_FONT_BUT,     /* 10x26 red button labels */
    M_FONT_STD,     /* 8x17 white */
    M_FONT_MID,     /* 10x26 white */
    M_FONT_COUNT
};

typedef struct {
    a2_archive mrcs;   /* Mrcs.img / Mrcs.ind */
    a2_archive menurc; /* MENURC.IMG / MENURC.IND */
    m_font font[M_FONT_COUNT];
    uint8_t pal[768];
} m_assets;

int m_assets_open(m_assets *a, const char *datadir);
void m_assets_close(m_assets *a);

/* MENURC entry `index` (0-based, as the binary index stores it). */
int m_sprite_get(const m_assets *a, int index, m_sprite *out);
int m_sprite_count(const m_assets *a);

/* A full-screen background (e.g. "hoofd.PCX") as M_W * M_H indices, malloc'd. */
uint8_t *m_background(const m_assets *a, const char *name);

/* A Mrcs.img entry's raw bytes (e.g. a .WAV), or NULL. */
const uint8_t *m_file(const m_assets *a, const char *name, uint32_t *size);

#endif /* A2_MASSETS_H */
