/* The options (m_opties.cpp) and options.ras.
 *
 * options.ras is the 18-byte options struct (0x43e7d8; load_options 0x408a40,
 * save_options 0x408ab0): i32 effects on, i32 music on, u8 effects volume,
 * u8 music volume, i32 resolution, i32 difficulty; missing, it is created
 * with 1, 1, 80, 80, 0, 1 (init_options 0x408b00).  At START,
 * ini_write_options (0x401820) puts them into Raser.ini -- RESOLUTION, and
 * SFXVOLUME / MUSICVOLUME as 3100 - 30 * volume when on, 0 when off -- and
 * Game.ini [OPTIONS] Difficulty.
 *
 * The screen (form 0x430f60): effects / music check boxes (anim 46, at
 * 248,130 / 248,202) and sliders (bar sprite 179 cut to the volume from x
 * 382, knob anim 41 at 382 + 128 * volume / 100; 151x30 hot areas at
 * 377,131 / 377,202; 0x40b9c0 sets 1..100 from the mouse while held);
 * resolution 320 x 200 (0), 640 x 480 (1) as small radios (anim 45, 257,313 /
 * 257,341) and "3D Kaart" (3) as a big box (anim 44, 407,322); difficulty
 * Makkelijk (0) / Moeilijk (1), big boxes at 250,427 / 407,427.  OK saves
 * options.ras, Annuleren reloads it.
 */
#include "menu.h"

#include "../a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void defaults(m_options *o)
{
    o->sfx_on = o->music_on = 1;
    o->sfx_vol = o->music_vol = 80;
    o->resolution = 0;
    o->difficulty = 1;
}

static int32_t le32(const uint8_t *b) { return (int32_t)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24); }

static void put32(uint8_t *b, int32_t v)
{
    for (int i = 0; i < 4; i++)
        b[i] = (uint8_t)(v >> (8 * i));
}

int m_options_save(const menu *m)
{
    uint8_t b[18];
    const m_options *o = &m->opt;
    put32(b, o->sfx_on);
    put32(b + 4, o->music_on);
    b[8] = (uint8_t)o->sfx_vol;
    b[9] = (uint8_t)o->music_vol;
    put32(b + 10, o->resolution);
    put32(b + 14, o->difficulty);
    char path[1024];
    snprintf(path, sizeof path, "%s/options.ras", m->datadir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t n = fwrite(b, 1, sizeof b, f);
    return fclose(f) == 0 && n == sizeof b ? 0 : -1;
}

void m_options_load(menu *m)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/options.ras", m->datadir);
    size_t size = 0;
    uint8_t *b = a2_read_file(path, &size);
    m_options *o = &m->opt;
    if (b && size == 18) {
        o->sfx_on = le32(b);
        o->music_on = le32(b + 4);
        o->sfx_vol = b[8];
        o->music_vol = b[9];
        o->resolution = le32(b + 10);
        o->difficulty = le32(b + 14);
    } else {
        defaults(o);
        m_options_save(m);
    }
    free(b);
}

void m_options_write_ini(const menu *m)
{
    const m_options *o = &m->opt;
    char v[16];
    snprintf(v, sizeof v, "%d", o->resolution);
    a2_ini_write(m->datadir, "Raser.ini", "[Program]", "RESOLUTION", v);
    snprintf(v, sizeof v, "%d", o->sfx_on ? 3100 - 30 * o->sfx_vol : 0);
    a2_ini_write(m->datadir, "Raser.ini", "[Sound]", "SFXVOLUME", v);
    snprintf(v, sizeof v, "%d", o->music_on ? 3100 - 30 * o->music_vol : 0);
    a2_ini_write(m->datadir, "Raser.ini", "[Sound]", "MUSICVOLUME", v);
    snprintf(v, sizeof v, "%d", o->difficulty);
    a2_ini_write(m->datadir, "Game.ini", "[OPTIONS]", "Difficulty", v);
}

/* draw_slider 0x40b880, on the hot areas 3 / 4 {151, 30} at x 377: the
 * track (anim 43) at (x, y); the knob (anim 41) at
 * x + 8 + (w - knob_w - 16) * v / 100, clamped to x + 1 .. x + track_w;
 * between them the fill (anim 42) Blt-stretched from its whole sprite onto
 * (x + 8, y + 5) .. (knob, y + h - 5) -- so the stripes widen with the value
 * (0x4122e0, IDirectDrawSurface::Blt with two rects); the knob at y + 2. */
#define BAR_X 377
#define BAR_W 151
#define BAR_H 30

static int knob_w(menu *m)
{
    m_sprite k;
    return m_sprite_get(&m->a, m_anim_frame(m, 41, 0), &k) == 0 ? k.w : 11;
}

void m_slider_draw(menu *m, m_canvas *c, int x, int y, int vol)
{
    m_sprite track, fill;
    int tw = BAR_W;
    if (m_sprite_get(&m->a, m_anim_frame(m, 43, 0), &track) == 0) {
        m_blit(c, &track, x, y);
        tw = track.w;
    }
    int kx = (BAR_W - knob_w(m) - 16) * vol / 100 + x + 8;
    if (kx <= x)
        kx = x + 1;
    if (kx - x > tw)
        kx = x + tw;
    if (m_sprite_get(&m->a, m_anim_frame(m, 42, 0), &fill) == 0)
        m_blit_stretch(c, &fill, x + 8, y + 5, kx, y + BAR_H - 5);
    m_draw_sprite(m, c, m_anim_frame(m, 41, 0), kx, y + 2);
}

void m_options_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    const m_options *o = &m->opt;
    m_text(c, std, 374, 78, "GELUID");
    m_text(c, std, 366, 274, "GRAPHICS");
    m_text(c, std, 326, 397, "Moeilijkheidsgraad");
    m_draw_sprite(m, c, m_anim_frame(m, 46, o->sfx_on ? 1 : 0), 248, 130);
    m_draw_sprite(m, c, m_anim_frame(m, 46, o->music_on ? 1 : 0), 248, 202);
    m_slider_draw(m, c, BAR_X, 131, o->sfx_vol);
    m_slider_draw(m, c, BAR_X, 202, o->music_vol);
    m_draw_sprite(m, c, m_anim_frame(m, 45, o->resolution == 0 ? 1 : 0), 257, 313);
    m_draw_sprite(m, c, m_anim_frame(m, 45, o->resolution == 1 ? 1 : 0), 257, 341);
    m_draw_sprite(m, c, m_anim_frame(m, 44, o->resolution == 3 ? 1 : 0), 407, 322);
    m_draw_sprite(m, c, m_anim_frame(m, 44, o->difficulty == 0 ? 1 : 0), 250, 427);
    m_draw_sprite(m, c, m_anim_frame(m, 44, o->difficulty == 1 ? 1 : 0), 407, 427);
    /* labels: Windows text in the original; stand-ins */
    m_gdi_text_ink(m, c, 31, 255, 104, "Geluidseffecten");
    m_gdi_text_ink(m, c, 31, 255, 175, "Muziek");
    m_gdi_text_ink(m, c, 31, 287, 312, "320 x 200");
    m_gdi_text_ink(m, c, 31, 287, 340, "640 x 480");
    m_gdi_text_ink(m, c, 31, 451, 328, "3D Kaart");
    m_gdi_text_ink(m, c, 31, 295, 433, "Makkelijk");
    m_gdi_text_ink(m, c, 31, 452, 433, "Moeilijk");
    m_gdi_text_ink(m, c, 31, 598, 466, "v1.01");
}

static int in(int x, int y, int rx, int ry, int w, int h)
{
    return x >= rx && x < rx + w && y >= ry && y < ry + h;
}

/* 0x40b9c0: (mouse x - x) * (100.0 / (w - knob_w)), truncated, 1..100 */
int m_slider_value(menu *m, int x0, int x)
{
    int v = (int)((float)(x - x0) * (100.0f / (float)(BAR_W - knob_w(m))));
    return v < 1 ? 1 : v > 100 ? 100 : v;
}

int m_options_click(menu *m, int x, int y)
{
    m_options *o = &m->opt;
    if (in(x, y, 377, 131, 151, 30))
        o->sfx_vol = m_slider_value(m, BAR_X, x);
    else if (in(x, y, 377, 202, 151, 30)) {
        o->music_vol = m_slider_value(m, BAR_X, x);
        m_music_update(m);
    } else if (in(x, y, 248, 130, 31, 31))
        o->sfx_on = !o->sfx_on;
    else if (in(x, y, 248, 202, 31, 31)) {
        o->music_on = !o->music_on;
        m_music_update(m); /* 0x408bbc: off stops the music, on starts it */
    }
    else if (in(x, y, 257, 313, 17, 17))
        o->resolution = 0;
    else if (in(x, y, 257, 341, 17, 17))
        o->resolution = 1;
    else if (in(x, y, 407, 322, 31, 31))
        o->resolution = 3;
    else if (in(x, y, 250, 427, 31, 31))
        o->difficulty = 0;
    else if (in(x, y, 407, 427, 31, 31))
        o->difficulty = 1;
    else
        return 0;
    return 1;
}

/* dragging a slider (0x40b9c0 follows the mouse while the button is held) */
void m_options_drag(menu *m, int x, int y)
{
    if (in(x, y, 377, 131, 151, 30))
        m->opt.sfx_vol = m_slider_value(m, BAR_X, x);
    else if (in(x, y, 377, 202, 151, 30)) {
        m->opt.music_vol = m_slider_value(m, BAR_X, x);
        m_music_update(m);
    }
}

void m_options_action(menu *m, int id)
{
    if (id == 10) { /* OK */
        m_options_save(m);
        m->next_screen = SCR_MAIN;
    } else if (id == 11) { /* Annuleren: back to what options.ras holds */
        m_options_load(m);
        m_music_update(m);
        m->next_screen = SCR_MAIN;
    }
}
