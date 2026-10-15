/* The game's 3D font.
 *
 * `FONT3D_ANIM` has 49 frames, each a `LTR_*.d3d` mesh textured with
 * `s_letter.pcx` -- the numbers and text drawn over the track (speed, lap and
 * checkpoint times, position) are extruded 3D glyphs, not sprites.  The frame
 * order is:
 *
 *   0..9    LTR_0 .. LTR_9
 *   10..35  LTR_A .. LTR_Z
 *   36      LTR_T again
 *   37..40  LTR_AE, LTR_OE, LTR_SS, LTR_UE   (German umlauts and eszett)
 *   41..48  LTR_IS, LTR_AANH, LTR_LINE, LTR_MAAL, LTR_PLUS, LTR_PUNT,
 *           LTR_UITR, LTR_VRAG
 *
 * The tail names are Dutch: aanhalingsteken (quote), line, maal (multiply),
 * punt (full stop), uitroepteken (!), vraagteken (?).
 */
#ifndef A2_FONT3D_H
#define A2_FONT3D_H

#include "archive.h"
#include "render.h"

typedef struct a2_font3d a2_font3d;

/* Load the glyph meshes into `scene`.  Returns NULL if none could be loaded. */
a2_font3d *a2_font3d_load(a2_scene *scene, const a2_archive *rcs);
void a2_font3d_free(a2_font3d *f);

/* Width of `text` in glyph units (height is 1.0). */
float a2_font3d_width(const a2_font3d *f, const char *text);

/* Draw `text` with its left edge at (x, y) in the current 2D overlay, `size`
 * pixels tall.  Call between a2_sprites_begin/end. */
void a2_font3d_draw(const a2_font3d *f, const a2_scene *scene, const char *text, float x, float y,
                    float size);

/* The scene model handle of character `c`'s glyph mesh, or -1, for drawing
 * a glyph in the world (the start countdown's flying letters). */
int a2_font3d_glyph(const a2_font3d *f, char c);

#endif /* A2_FONT3D_H */
