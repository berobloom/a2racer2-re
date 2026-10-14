/* The racing cars' wheels, after car_whl.cpp.
 *
 * The seven player-car meshes (r_*.d3d, CARS.DAT slots 0-6) have no wheels:
 * the original hangs four copies of WIEL_ANIM (wiel.d3d, radius 3.5) on each
 * as sub-objects.  0x4136c0 places them from a per-type table (0x486198);
 * every frame 0x413900 (the player) or 0x413be0 (a racer) spins them, steers
 * the front pair and sets their height from the road.  Traffic, police and
 * ADAC meshes carry their wheels in the mesh and get none.
 */
#ifndef A2_WHEELS_H
#define A2_WHEELS_H

#include "render.h"

#define A2_WHEEL_TYPES 7

typedef struct {
    float spin;   /* sub-object +0x74, all four wheels, 4096 units per turn */
    float steer;  /* +0x78 of the front pair (wheels 0, 1) */
    float y;      /* the four wheels' height in the car's frame */
} a2_wheels;

/* 0x4136c0: at rest, at the table's height. */
void a2_wheels_init(a2_wheels *w);
/* 0x413900: the player's, steered by the steering input (-100..100). */
void a2_wheels_update_player(a2_wheels *w, float v_fwd, float steer_input,
                             float ground_minus_y, float dt);
/* 0x413be0: a racer's, steered by its yaw rate (rad/s). */
void a2_wheels_update_ai(a2_wheels *w, float v_fwd, float yaw_rate, float ground_minus_y,
                         float dt);
/* Draws the four wheels in the current matrix, which must be the car's own
 * (origin at its position, after its rotation).  No-op for a type outside 0-6. */
void a2_wheels_draw(const a2_wheels *w, const a2_scene *s, int wheel_model, int car_type);

#endif
