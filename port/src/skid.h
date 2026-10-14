/* Skidmarks, after gfx_skid.cpp.
 *
 * The player's car only.  While the skid intensity is 4 or more the original
 * (0x41dd00) extends a strip every frame with one cross-section per rear
 * wheel; a strip starts when the intensity rises to 4 from below, holds at
 * most 100 vertices a wheel, and lives 20 seconds (fading out over that time
 * with Game.ini fade_skmarks).  There are 20 strips.  A cross-section is 4
 * units wide, laid across the direction the car is moving, at the rear axle
 * 7 units either side of the centre line.  Marks darken the road by
 * (255 - alpha) / 255 (INVSRCALPHA / SRCALPHA blending), with the alpha set
 * from the intensity: 200 - 16 * (intensity - 4).  See "Skidmarks" in
 * research/discoveries.md.
 */
#ifndef A2_SKID_H
#define A2_SKID_H

typedef struct a2_skid a2_skid;

/* `tex`: the S_SFX texture, or 0 to draw the marks untextured. */
a2_skid *a2_skid_create(int fade, unsigned tex);
void a2_skid_free(a2_skid *s);
/* One frame.  The car is at (x, y, z) with heading psi (radians, the
 * original's convention: lateral axis (cos, sin), forward (-sin, cos) in
 * world x/z), local velocity (v_lat, v_y, v_fwd), and its rear axle `rear`
 * units behind the centre of gravity. */
void a2_skid_lay(a2_skid *s, int intensity, float x, float y, float z, float psi, float v_lat,
                 float v_y, float v_fwd, float rear, float dt);
/* Age and fade every strip by one frame. */
void a2_skid_update(a2_skid *s);
void a2_skid_draw(const a2_skid *s);

#endif /* A2_SKID_H */
