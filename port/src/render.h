/* Scene builder + drawer for one loaded track.
 *
 * Geometry is flattened once at load time into per-plane-list vertex/index
 * arrays, with a per-chunk index range recorded so chunks can be culled the way
 * the original does (game.ini `visiblechunks=30`).
 */
#ifndef A2_RENDER_H
#define A2_RENDER_H

#include "anims.h"
#include "archive.h"
#include "track.h"

typedef struct a2_scene a2_scene;

/* `plane_tex`, when non-NULL, names the four `.rmp` plane-list textures in list
 * order -- exactly Raser.ini [MAPS] args 2-5, which is where the caller should
 * get them.  Each may carry an extension or not.  When it is NULL the loader
 * falls back to `prefix` (the track's texture prefix, e.g. "e00") plus the
 * candidate suffixes in render.c.
 * `atlas`, when non-NULL, overrides all four with one archive texture. */
a2_scene *a2_scene_create(const a2_track *tr, const a2_archive *rcs, const char *prefix,
                          const char *const plane_tex[4], const char *atlas,
                          const a2_anims *anims);
void a2_scene_destroy(a2_scene *s);

/* Draw chunks whose centre is within `radius` chunks of `near_chunk`.
 * `show_objects` toggles the .omp scenery. */
void a2_scene_draw(const a2_scene *s, int near_chunk, int radius, int show_objects);

/* Index of the chunk whose centre is nearest to (x, z). */
int a2_scene_nearest_chunk(const a2_scene *s, float x, float z);
/* Start pose: above the first chunk, facing along the track (chunk 0 -> 1).
 * `out_yaw` is in degrees, matching the viewer's camera convention. */
void a2_scene_start_position(const a2_scene *s, float out[3], float *out_yaw);

/* Start pose for a car: lane `lane` of `chunk` (clamped to the lanes there),
 * taken from the .dmp waypoint graph, facing along the road to the next
 * chunk.  Returns 0 when the track has no waypoints. */
int a2_scene_start_pose_on_lane(const a2_scene *s, int chunk, int lane, float out[3],
                                float *out_yaw);

/* Number of lanes the player's carriageway has in `chunk` (3 on the shipped
 * tracks), and the world position of one of them. */
int a2_scene_lane_count(const a2_scene *s, int chunk);
/* Number of chunks in the track. */
int a2_scene_chunk_count(const a2_scene *s);
/* A chunk's own position (.pmp x, y, z). */
int a2_scene_chunk_point(const a2_scene *s, int chunk, float out[3]);

/* Knock-over props: pylons, barriers and speed cameras (.omp types 2, 3, 7).
 * One frame of their type handlers for the chunks lo..hi, against the
 * player at (px, pz) with radius pradius and the model's v_lat / v_fwd and
 * heading (4096 a turn).  Returns the speed cameras knocked over this frame
 * (the radar bonus, 0x4dc964). */
int a2_scene_props_step(a2_scene *s, int lo, int hi, float px, float pz, float pradius,
                        float v_lat, float v_fwd, float heading, unsigned *rng);
/* The props' pending samples (object +0x14), for the positional slots; the
 * caller clears each once it has played (0x428bc0). */
#define A2_PROP_KEY 100000
typedef struct {
    int key;
    float x, z;
    int sample;
} a2_prop_sound;
int a2_scene_prop_sounds(const a2_scene *s, a2_prop_sound *out, int max);
void a2_scene_prop_sound_done(a2_scene *s, int key);
/* Collide a car at (x, z) against solid scenery near `chunk`.  Solid are the
 * .omp types the original registers as collision candidates: 5 columns, 8
 * arrows, 10 police placements (0x4454a7); everything else -- lamp posts,
 * trees, the knock-over props -- never blocks.  The `.cmp` rails remain the
 * game's main collision data.
 * Returns 1 on a hit, pushing (x, z) clear and writing the outward normal. */
/* Stop drawing the .omp objects of `type` (the cops module draws type 9). */
void a2_scene_hide_type(a2_scene *s, int type);
int a2_scene_collide_objects(const a2_scene *s, int chunk, float *x, float *z, float radius,
                             float push[2], int *type_hit, int *inst_hit);
/* Pushable scenery (types 8 and 10): the instance's position, and a kick
 * from the collision impulse (world m/s, rad/s). */
int a2_scene_object_pos(const a2_scene *s, int inst, float *x, float *z);
void a2_scene_object_push(a2_scene *s, int inst, float dvx, float dvz, float dw);
/* One frame of the pushed objects' motion: they move by their velocity and
 * are damped by their type handlers, arrows x0.95 a frame (0x4296b0), parked
 * police cars x0.9 (0x4293a0). */
void a2_scene_objects_step(a2_scene *s, int lo, int hi, float dt);

/* Clock for the animated props -- the two traffic lights and three roadworks
 * arrows, the only anims in the game with more than one frame that are placed
 * as objects.  Pass the race or wall clock in seconds. */
void a2_scene_set_anim_time(a2_scene *s, float seconds);
int a2_scene_lane_point(const a2_scene *s, int chunk, int lane, float out[3]);
/* Index of the lane in `chunk` whose waypoint is nearest (x, z), or -1. */
int a2_scene_nearest_lane(const a2_scene *s, int chunk, float x, float z);
/* Human-readable summary for the status line. */
void a2_scene_stats(const a2_scene *s, char *buf, size_t buflen);

/* Load one extra model by anim name (e.g. "RACER_1_ANIM") so the viewer can
 * place a car.  Returns a handle for a2_scene_draw_model, or -1. */
int a2_scene_load_model(a2_scene *s, const a2_archive *rcs, const a2_anims *anims,
                        const char *anim_name);
void a2_scene_draw_model(const a2_scene *s, int handle, float x, float y, float z, float yaw_deg);
/* Draw a model at the origin of the current matrix with every vertex the one
 * colour and alpha-blended (SRCALPHA / INVSRCALPHA), as gfx_lght.cpp's
 * object lights are: 0x41b400 tints the mesh, 0x41b720 sets the blend. */
/* Give a loaded model a "glow" copy of a texture: white, with the texel's
 * brightness (after the gamma) as its alpha and the colour key clear.  The
 * object lights look like this in the original (a soft glow over the tail
 * lights, captured under Wine with the lights forced on); how its Direct3D
 * setup turns brightness into coverage is not traced. */
int a2_scene_set_glow_texture(a2_scene *s, const a2_archive *rcs, int handle, const char *tex_name);
void a2_scene_draw_model_tinted(const a2_scene *s, int handle, uint8_t r, uint8_t g, uint8_t b,
                                uint8_t a);
/* As above, plus body pitch and roll -- for the suspension lean driven by
 * CARS.DAT's roll/pitch damping and factor pairs. */
void a2_scene_draw_model_rpy(const a2_scene *s, int handle, float x, float y, float z,
                             float yaw_deg, float pitch_deg, float roll_deg);

/* Load a model by explicit mesh and texture archive names (the 3D font's glyphs
 * are meshes, not anims).  Returns a handle for a2_scene_draw_model, or -1. */
int a2_scene_load_mesh_named(a2_scene *s, const a2_archive *rcs, const char *mesh_name,
                             const char *tex_name);
/* Axis-aligned bounds of a loaded model.  Returns 0 for an unknown handle. */
int a2_scene_model_bounds(const a2_scene *s, int handle, float lo[3], float hi[3]);
/* Draw a model with no rotation, at an explicit uniform scale. */
void a2_scene_draw_model_scaled(const a2_scene *s, int handle, float x, float y, float z,
                                float scale);

/* A texture from the scene's cache (loading it on first use), by registry
 * name; 0 when it cannot be loaded. */
unsigned a2_scene_texture(a2_scene *s, const a2_archive *rcs, const char *name);
/* Stamp `rgba` (w x h) into an already loaded texture at (x, y), the way the
 * original's blit_anim_frame_to_texture (0x42ae90) stamps sprites into a
 * texture sheet.  `tex_name` is matched on its stem, so "RACER01.PCX" finds
 * the sheet whichever of .pcx/.tga was loaded.  Returns 0 if not loaded. */
int a2_scene_blit(const a2_scene *s, const char *tex_name, int x, int y, int w, int h,
                  const uint8_t *rgba);
/* Car shadows (gfx_shdw.cpp): load S_SHADOW_ANIM's texture once, then draw a
 * car's shadow -- its mesh footprint, flat on the road under (x, z), shifted by
 * the original's fixed (2, 2) and turned by the same yaw the model is drawn
 * with.  Call before drawing the car. */
int a2_scene_load_shadow(a2_scene *s, const a2_archive *rcs, const a2_anims *anims);
void a2_scene_draw_shadow(const a2_scene *s, int handle, float x, float z, int chunk,
                          float yaw_deg);
/* Ground height at (x, z): the chunk's ground plane (0x442f00), defined
 * everywhere, so off the road too.  `chunk` is the nearest chunk.  Returns 0
 * and leaves *y alone only for a bad chunk. */
/* On a circuit (the last chunk runs on into chunk 0) a chunk index taken
 * modulo the chunk count; elsewhere the index, or -1 off either end. */
int a2_scene_wrap_chunk(const a2_scene *s, int c);
/* A solid scenery instance as a collision box (the mesh's XZ bounds, its
 * position and heading in the engine's convention); 0 if there is none. */
struct a2_cbox;
int a2_scene_object_box(const a2_scene *s, int inst, struct a2_cbox *box);
/* The barriers of `chunk` and the chunk before it as collision boxes, as
 * 0x449c10 makes them: 10 units thick behind the rail (its -x side; the road
 * is on +x for 99% of the shipped rails), 1 unit past each end, at the
 * rail's start and angle.  Returns how many were written. */
int a2_scene_rail_boxes(const a2_scene *s, int chunk, struct a2_cbox *out, int max);
/* The solid scenery of `chunk` and its neighbours as collision boxes, with
 * their instance numbers.  Returns how many were written. */
int a2_scene_solid_boxes(const a2_scene *s, int chunk, struct a2_cbox *out, int *inst, int max);
/* A scenery instance's .omp type, or -1. */
int a2_scene_object_type(const a2_scene *s, int inst);
/* to - from, the short way round on a circuit. */
int a2_scene_chunk_delta(const a2_scene *s, int from, int to);
int a2_scene_road_height(const a2_scene *s, int chunk, float x, float z, float *y);

/* Push (x, z) out of any .cmp rail segment closer than `radius`, looking at
 * `chunk` and its immediate neighbours.  Returns 1 if anything was hit and
 * writes the barrier's unit direction to `slide` so the caller can keep the
 * component of its motion that runs along the rail. */
int a2_scene_collide_rails(const a2_scene *s, int chunk, float *x, float *z, float radius,
                           float slide[2]);

/* Debug overlay: draw the rail segments of the visible chunks. */
void a2_scene_draw_rails(const a2_scene *s, int near_chunk, int radius);

#endif /* A2_RENDER_H */
