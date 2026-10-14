/* Tyre smoke, after gfx_wolk.cpp (Game.ini's "clouds").
 *
 * While the player's skid intensity is 4 or more the original spawns, every
 * 100 ms, two puffs at each rear wheel (0x425a90 -> 0x41fff0 -> 0x41fd80) on
 * 70 slots.  A puff is a camera-facing fan, blue-grey at the centre and
 * transparent at the rim, that drifts with a share of the car's velocity,
 * rises, grows from 7 to 36 units and fades.  Its update is per frame, as in
 * the original (0x41fae0).  See "gfx_wolk.cpp clouds are tyre smoke" in
 * research/discoveries.md.
 */
#ifndef A2_SMOKE_H
#define A2_SMOKE_H

typedef struct a2_smoke a2_smoke;

a2_smoke *a2_smoke_create(void);
void a2_smoke_free(a2_smoke *s);
/* One spawner call: two puffs at (x, y, z) moving at (vx, vz) world units per
 * frame, with a lifetime of 4 seconds' worth of frames at this `dt`. */
void a2_smoke_spawn(a2_smoke *s, float x, float y, float z, float vx, float vz, float dt);
/* Advance every live puff by one frame. */
void a2_smoke_update(a2_smoke *s);
/* Draw them facing the camera (uses the current GL modelview). */
void a2_smoke_draw(const a2_smoke *s);

#endif /* A2_SMOKE_H */
