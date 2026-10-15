/* The game's bitmap fonts (`.FNT` in RCS.IMG), as used by the in-race HUD.
 *
 * Format, from `Load_new_ascii_set` (fcn.0046A2D0, `gfx_asci.cpp`), which does
 * `fread(table, 8, 256, f)` before touching any pixels:
 *
 *     struct { u16 width; u16 height; u32 file_offset; } glyph[256];  // 2048 B
 *     ... glyph bitmaps at the offsets named above ...
 *
 * One entry per ASCII code; an absent glyph has every byte 0xFF, which is why
 * these files open with a long run of them.  Each bitmap is `width * height`
 * palette indices with **0xFF transparent**, the same key the sprites use.
 *
 * The arithmetic checks out exactly: in `LoKMTel.fnt` the glyph for '0' is
 * 26x24 at offset 2056 and '1' starts at 2680, and 2680 - 2056 = 624 = 26*24.
 *
 * spel.dat keeps a table of 34 font descriptors at VA 0x486D08, 148 bytes each,
 * indexed as **resolution*17 + font** (fcn.00418330 bounds-checks both and
 * complains with "Incorrect resolution index" / "Incorrect font index"), so
 * there are 17 fonts in a lores and a hires set:
 *
 *   0 lo_fnt3 / olfnt03    1 lomid / himid      2 logeel / higeel
 *   3 lo_geld / hi_geld    4 lo_vlag / hi_vlag  5 lo_bts / hi_bts
 *   6 lo_fnt1 / olfnt07    7 marc10             8 slider13
 *   9 slider15            10 lo_fnt1 / olfnt01  11 lo_fnt2 / olfnt02
 *  12 lo_fnt4 / olfnt04   13 lo_fnt1 / olfnt06  14 lo_fnt8 / olfnt08
 *  15 lo_fnt9 / olfnt09   16 lo_fnt1
 *
 * `LoKMTel.fnt` / `HiKMTel.fnt` -- *km-teller*, the speedometer digits -- are
 * not in that table and are loaded separately; they hold only '0'..'9'.
 */
#ifndef A2_FONT_H
#define A2_FONT_H

#include "archive.h"

#include <stdint.h>

typedef struct a2_font a2_font;

/* Load `name` out of `rcs` and colour it through a 768-byte RGB palette.
 * Returns NULL if the entry is missing or malformed. */
a2_font *a2_font_load(const a2_archive *rcs, const char *name, const uint8_t palette[768]);
void a2_font_free(a2_font *f);

/* Letter spacing added to every glyph's advance, in the font's own pixels:
 * the original's 0x469f70 / 0x4182f0, set per font at load (0x4183c0 ..
 * 0x418737); at 640x480 olfnt01 -2, olfnt02 -4, olfnt04 -3, olfnt03 -8. */
void a2_font_set_spacing(a2_font *f, int px);
/* Width and height of `text` at `scale`, in pixels. */
float a2_font_text_width(const a2_font *f, const char *text, float scale);
float a2_font_height(const a2_font *f, float scale);

/* Draw `text` with its top-left at (x, y).  Call inside a2_sprites_begin/end:
 * it uses the same 2D overlay state.
 *
 * The glyph art is **greyscale** in every palette the game ships -- the
 * km-teller's busiest index is a near-white (191,183,183) -- yet the speed
 * readout is blue on screen, so the colour is applied when drawing, not baked
 * in. `a2_font_draw` keeps the art as-is; the `_tinted` form multiplies it. */
void a2_font_draw(const a2_font *f, const char *text, float x, float y, float scale);
void a2_font_draw_tinted(const a2_font *f, const char *text, float x, float y, float scale,
                         float r, float g, float b);

#endif /* A2_FONT_H */
