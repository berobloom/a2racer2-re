/* Scripted path objects (.pth), after pth_main.cpp / pth_heli.cpp: the canal
 * boats, trains, helicopter, aeroplane and scripted traffic.
 *
 * A path starts when the player enters its first_chunk on its lap (f3, 0 =
 * every lap), at most once per lap, on one of ten slots.  The object follows a
 * Catmull-Rom curve through the authored points with every segment taking the
 * same share of the path's time (f4), stays at the end, and is released when
 * the player reaches the path's last_chunk.  See "How a path object moves" in
 * research/discoveries.md.
 */
#ifndef A2_PATHS_H
#define A2_PATHS_H

#include "anims.h"
#include "archive.h"
#include "audio.h"
#include "render.h"
#include "track.h"

typedef struct a2_paths a2_paths;

/* Load the meshes the track's paths need.  NULL when the track has none. */
a2_paths *a2_paths_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                          const a2_track *track);
void a2_paths_free(a2_paths *p);
/* The player has entered `chunk`, with `laps_done` laps completed (the
 * original's 0x4d9ad4).  Starts and releases paths (fcn.00434460). */
void a2_paths_on_chunk(a2_paths *p, int chunk, int laps_done);
/* Advance the active objects by one frame of `dt` seconds. */
void a2_paths_update(a2_paths *p, float dt);
void a2_paths_draw(const a2_paths *p, const a2_scene *scene);
/* The active objects as sound sources (keys 4000+), samples filled in. */
int a2_paths_sound_sources(const a2_paths *p, a2_sound_src *out, int max);
int a2_paths_active(const a2_paths *p);
/* Position and heading (degrees) of the first active object; 0 if none.  For
 * the A2_PATH_CAM debug view. */
int a2_paths_first(const a2_paths *p, float pos[3], float *yaw);

#endif /* A2_PATHS_H */
