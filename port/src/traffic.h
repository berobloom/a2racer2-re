/* Traffic cars driving the .dmp lane graph.
 *
 * The graph is the game's own driving line: three or more lanes per
 * carriageway, one waypoint per lane per chunk.  Rather than simulate steering,
 * a traffic car simply advances along the segment between its lane's waypoint
 * in chunk c and the one in chunk c+1, which keeps it exactly on the road
 * through every curve.  That is close to what ai_path/ai_lane do in the
 * original, without (yet) the lane changing, overtaking or police behaviour.
 *
 * Each car's mesh and texture come from the anim table in spel.dat
 * (N_GOLF_ANIM -> n_golf.d3d + cars02.pcx, and so on).
 */
#ifndef A2_TRAFFIC_H
#define A2_TRAFFIC_H

#include "anims.h"
#include "archive.h"
#include "audio.h"
#include "render.h"
#include "track.h"

typedef struct a2_traffic a2_traffic;

/* Create `count` traffic cars spread over the chunks around `start_chunk`.
 * Returns NULL if no models could be loaded. */
a2_traffic *a2_traffic_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                              int count, int start_chunk);
void a2_traffic_free(a2_traffic *t);

/* Give traffic the per-chunk speed limits it obeys, read from the .pmp speed
 * triggers the way the original's AI reads them (see traffic.c). */
void a2_traffic_set_limits(a2_traffic *t, const a2_track *track);

/* Advance every car and recycle any that fall too far behind `player_chunk`. */
void a2_traffic_update(a2_traffic *t, const a2_scene *scene, float dt, int player_chunk);
/* The other vehicles the traffic sees in its lanes this frame (the player,
 * the racers): every vehicle stands on a waypoint in the original, and the
 * lane scan finds whatever is there (ai_lane.cpp). */
typedef struct {
    float x, y, z, speed; /* speed in m/s */
} a2_traffic_other;
void a2_traffic_set_others(a2_traffic *t, const a2_traffic_other *o, int n);

/* Draw the cars within `radius` chunks of `player_chunk`. */
/* The live traffic cars as sound sources (keys 2000+), anim names filled in
 * and samples left at -1 for the caller to resolve. */
int a2_traffic_sound_sources(const a2_traffic *t, a2_sound_src *out, int max);
/* Traffic car i as a collision box; 0 if it is not on the road. */
struct a2_cbox;
int a2_traffic_car_box(const a2_traffic *t, int i, struct a2_cbox *box);
void a2_traffic_draw(const a2_traffic *t, const a2_scene *scene, int player_chunk, int radius);

int a2_traffic_count(const a2_traffic *t);

/* World position of car `i`, for the radar.  Returns 0 if it is not alive. */
int a2_traffic_car_xz(const a2_traffic *t, int i, float out[2]);
float a2_traffic_car_speed(const a2_traffic *t, int i); /* m/s, 0 if none */
/* How many lane changes the traffic has made -- a diagnostic, since the
 * behaviour is otherwise hard to see from outside. */
int a2_traffic_lane_changes(const a2_traffic *t);

/* Collide a car at (x, z) with the traffic.  On a hit the traffic car is
 * slowed, (x, z) is pushed clear, and the outward normal is written to `push`.
 * Returns 1 if anything was hit. */
int a2_traffic_collide(a2_traffic *t, float *x, float *z, float radius, float push[2], int *who);
/* Car i's world velocity (m/s) and anim name, for the collision impulse. */
int a2_traffic_car_motion(const a2_traffic *t, int i, float *vx, float *vz, const char **anim);
/* Add a world velocity change to car i: the traffic follows its lane, so
 * only the part along its heading is kept. */
void a2_traffic_car_push(a2_traffic *t, int i, float dvx, float dvz);

/* 1 if any live traffic car is within `radius` of (x, z).  Probing a lane's
 * waypoints a few chunks ahead with this is how the autopilot decides whether
 * its lane is blocked and which neighbouring lane is clear to pull into. */
int a2_traffic_occupied(const a2_traffic *t, float x, float z, float radius);

#endif /* A2_TRAFFIC_H */
