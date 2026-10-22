/* The sky backdrop.
 *
 * The shipped, Direct3D-accelerated game does not use the scrolling panorama
 * below.  Its race setup (0x4234e0) takes one of seven 128x128 cloud images,
 * `back<rand() % 7 + 1>.pcx` from the table at 0x48a790, builds a material
 * with it (0x45ce90 -> 0x45a460, 0x44f1a0) and sets that as the viewport's
 * background (0x42a210: SetMaterial, then IDirect3DViewport::SetBackground).
 * A Wine trace of the accelerated game opens back7.pcx / back2.pcx and never
 * SCRL_HI.pcx.
 *
 * That viewport background is only the clear colour (its material's diffuse,
 * white).  What shows is Gfx_bkg.cpp's own pair of screen quads (session 16,
 * read back from the running game): 0x4190d0 draws them every frame as
 * D3DTLVERTEX triangle fans over the whole viewport, the upper third with
 * v 0..1/3 and the rest with v 1/3..1, a u window 0.15 wide starting at
 * 1 - (player heading & 0x3ff) / 1024, so the sky turns with the car and
 * repeats every quarter turn.  The top and bottom edges are white and the
 * split line takes a horizon tint that 0x418f00 works out from the player's
 * progress round the track (a2_sky_horizon).  A render of back1 through
 * those quads matches a Wine capture to 3.6/255 in the visible sky; the
 * texture is not run through the game's gamma.
 *
 * The software renderer's scrolling sky (`gfx_sky.cpp`), kept behind
 * --scrolling-sky:
 *
 * Raser.ini's [MAPS] table names the sky per track in args 6 and 7 -- the
 * developers' comment calls them the "Lores/Hires unaccelerated filename of
 * the sky" -- and every track names the same pair, `scrl_lo` / `scrl_hi`.
 * Both ship in RCS.IMG: SCRL_LO.pcx is 480x200 and SCRL_HI.pcx 960x320.
 * "scrl" is scroll: it is a panorama blitted behind the 3D scene, not a mesh
 * or a dome, and it is bound outside the anim system (no `scrl` entry exists
 * in spel.dat's texture-name table).
 *
 * The original's maths, from `init_moving_sky` (fcn.0041E740) and the scroll
 * update (fcn.0041EAC0):
 *
 *   band height  = screen_h - screen_h/3          (320 of 480 in the shipped
 *                                                  mode, so SCRL_HI is 1:1)
 *   scroll_x     = (T[heading] + 1) * -(imgW/2)   wrapped into [0, imgW)
 *
 * where T is a 1024-entry full-circle trig table (the index is
 * `atan2(...) * 1024 / 6.2831855` masked with 0x3FF) and the -1.0 addend is
 * the constant at 0x47F6F0.  The table is built at runtime, so whether it is
 * sine or cosine cannot be read out of the image; sine is assumed here.
 * fcn.0041E9D0 then splits the source window into one or two blit rectangles,
 * which is only DirectDraw's inability to wrap a blit -- GL_REPEAT does it in
 * one quad.
 */
#ifndef A2_SKY_H
#define A2_SKY_H

#include "archive.h"

typedef struct a2_sky a2_sky;

/* `back`: 1..7 loads back<N>.pcx as the accelerated game's still backdrop;
 * 0 loads SCRL_HI.pcx, the software renderer's scrolling panorama.  Returns
 * NULL if the image is not in RCS.IMG. */
a2_sky *a2_sky_load(const a2_archive *rcs, int back);
void a2_sky_free(a2_sky *s);

/* Draw the backdrop over a `w` x `h` window.  The panorama goes across the
 * top, scrolled for a camera heading of `yaw_deg`; back<N> uses the player's
 * `heading` (4096 a turn) and the `horizon` tint (0xRRGGBB, a2_sky_horizon).
 * Call before the 3D scene: it writes no depth. */
void a2_sky_draw(const a2_sky *s, float yaw_deg, float heading, unsigned horizon, int w, int h);

/* 0x418f00: the horizon tint for the player at `chunk` of an `n_chunks` track
 * whose race started at `start`. */
unsigned a2_sky_horizon(int n_chunks, int start, int chunk);

#endif /* A2_SKY_H */
