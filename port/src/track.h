/* Per-track map files (etappe0..6).  Layouts in
 * reverse-engineering/file-formats.md; all five parse byte-exactly.
 *
 * A stage is a chain of 726-840 chunks spaced 128 world units apart:
 *   .rmp  per chunk, four lists of 104-byte "planes" (textured quads)
 *   .pmp  per chunk, world position + index lists into .cmp and .dmp
 *   .cmp  rails (guardrails)
 *   .dmp  the AI waypoint graph
 *   .omp  static scenery objects, each tagged with its chunk
 *   .pth  scripted object paths -- the helicopter, the aeroplane, a police car
 */
#ifndef A2_TRACK_H
#define A2_TRACK_H

#include <stddef.h>
#include <stdint.h>

#define A2_RMP_LISTS 4
#define A2_PLANE_LEN 104
#define A2_CHUNK_WP_MAX 12
#define A2_OMP_NAME_LEN 36

/* The four .rmp plane lists.  Only the first two are named by the loader's
 * overflow messages ("road-planes" max 16, "land-planes" max 24); the other two
 * are unidentified. */
enum { A2_PLANE_ROAD = 0, A2_PLANE_LAND = 1, A2_PLANE_LIST2 = 2, A2_PLANE_LIST3 = 3 };

typedef struct {
    float x, y, z, u, v;
} a2_plane_vertex;

typedef struct {
    a2_plane_vertex v[4];
    uint32_t tri[2][3]; /* indices 0..3, local to this plane */
} a2_plane;

typedef struct {
    a2_plane *planes[A2_RMP_LISTS];
    uint32_t counts[A2_RMP_LISTS];
} a2_rmp_chunk;

typedef struct {
    a2_rmp_chunk *chunks;
    uint32_t chunk_count;
    uint32_t plane_count_hint; /* stale in most tracks; not used for parsing */
} a2_rmp;

typedef struct {
    float x, y, z;
    /* Per-chunk trigger slots.  CONFIRMED from the engine: the dispatcher at
     * 0x442607 reads exactly these two words, at **+0xEC and +0xF0** of the
     * 388-byte runtime chunk record, looks each up in a 257-slot handler array
     * at 0x4E5928 and calls it with the car index -- if the slot is non-null.
     * The game registers handlers for values **0..48 only**, by walking its own
     * table at 0x48C488..0x48C54C (0x42a2b0), so everything else is inert.
     *
     *   1  the speed camera -- its handler plays sample 11, `flits.wav`
     *   2  the service station (`BENZINE_STATION`): refills the turbo tank to
     *      100, resets the damage, plays sample 47 `repair.wav` and raises
     *      banner 48 QUICK SERVICE.  It is a service stop, not a petrol stop --
     *      it never touches a fuel quantity.
     *   3, 48  share one handler, 0x415990 (unidentified; absent from the data)
     *   4/5/6/8  the posted speed limit: they write 50 / 70 / 100 / 120 into
     *      0x48C480, which is what the camera compares against, and blit sign
     *      face 0/1/2/3.  Note the set is 4, 5, 6 and **8** -- not 4-7.  The
     *      *police* limits are a separate system (2x default / 60 / 80 / 120,
     *      with a x1.1 tolerance) read by the cop AI; see discoveries.md.
     *   7  sets a flag, 0x4a70d4 -- not a limit
     *   9  ZERO_CHUNK_MARKER (circuits, chunk 0)
     *   13 start line; clears 0x48c888
     *   14 FINISH_POSITION; 15 FINISH_VALID
     *   16 sets an (R,G,B) of (0,0,255) with 3.5 and 2000.0 and an enable flag
     *      at 0x4dc9a8 -- LIKELY fog or haze; 17 looks like its "off".  Neither
     *      appears in any shipped track.
     *   22-39 traffic signs on the highway stages.  tr_vbord.cpp has 18
     *      handlers, every one gated to etappe 1/3/5, and the values appear on
     *      exactly those three stages.  The signs are not placed as geometry:
     *      each handler calls the texture blitter 0x42ae90 with a slot picked
     *      by the etappe and a **frame picked by the trigger value**, so 22+k
     *      and 31+k stamp frame k into a_TRFFC1 at (9, 122) and (9, 7) -- two
     *      gantries, one per carriageway, four faces each.  The gantry itself
     *      never changes; only its texture does.
     *   45 the checkpoint -- CONFIRMED by having a handler, 0x42a530, rather
     *      than only by its data correlation with the A_CHECK gantries
     *      (check_racer_trigger is a different dispatcher and covers only
     *      2, 9, 14, 15, 46 and 47)
     *
     * Null in the table, so inert through this path: 0, 10-12, **18-21**,
     * **40-44**, 46, 47.  For the lane triggers 18-21 and 40-43 that is the
     * point -- they are the commonest values in the data (332 of them) and the
     * lane system reads the chunk slots directly.  46 FUEL_APPROACH and
     * 47 FINAL_APPROACH never appear in track data at all.
     * The stray data values **86, 94, 107, 130, 150 and 196** are likewise
     * inert: all are <= 196 so they index inside the zeroed array, and the
     * dispatcher skips a null slot.  Editor leftovers. */
    uint32_t trigger0, trigger1;
    uint32_t *rails;
    uint32_t rail_count;
    uint32_t wp_left[A2_CHUNK_WP_MAX];
    uint32_t wp_left_count;
    uint32_t wp_right[A2_CHUNK_WP_MAX];
    uint32_t wp_right_count;
} a2_pmp_chunk;

typedef struct {
    a2_pmp_chunk *chunks;
    uint32_t chunk_count;
} a2_pmp;

typedef struct {
    float x, y, z;    /* position */
    float dx, dy, dz; /* delta; dz is typically +/-128, one chunk length */
} a2_rail;

typedef struct {
    uint32_t type;
    uint32_t flag;    /* .dmp field 1, runtime +4: 2 on the service stations'
                       * pump waypoints (two per station), 0 elsewhere */
    float x, y, z;    /* world position; y matches the owning chunk's for all
                       * but 9 of the 28461 waypoints, which sit off-centre */
    int32_t links[4]; /* waypoint indices, -1 = none */
} a2_waypoint;

typedef struct {
    int32_t chunk;
    int32_t type;
    float x, y, z;
    float rot_x, rot_y, rot_z; /* degrees; only rot_y is ever non-zero */
    char name[A2_OMP_NAME_LEN + 1];
} a2_object;

/* One point of a scripted path: the flag dword is 0 in every shipped track. */
typedef struct {
    uint32_t flag;
    float x, y, z;
} a2_path_point;

/* A `.pth` scripted path, from the type table at 0x4966F0: type 2 a police
 * car, 3 LIKELY a canal boat (E00_ROND_ANIM), 4 the train (C_TREIN_ANIM),
 * 5 the helicopter (`pth_heli.cpp`), 6 a random traffic vehicle, 7 the
 * aeroplane (A_VLIEGT_ANIM).  `first_chunk`/`last_chunk` bracket the stretch of
 * road it covers (last_chunk == 0 means unset).  **f2 is the path type**,
 * bounded 0..7, **f3 the lap it plays on** (1-based, 0 = every lap;
 * fcn.00434460 starts it when the player reaches first_chunk), and **f4 its
 * time in milliseconds** (5000 to 50000).  See
 * reverse-engineering/file-formats.md. */
typedef struct {
    uint32_t first_chunk, last_chunk;
    uint32_t f2, f3, f4;
    a2_path_point *points;
    uint32_t point_count;
} a2_path;

typedef struct {
    a2_rmp rmp;
    a2_pmp pmp;
    a2_rail *rails;
    uint32_t rail_count;
    a2_waypoint *waypoints;
    uint32_t waypoint_count;
    a2_object *objects;
    uint32_t object_count;
    a2_path *paths;
    uint32_t path_count;
    int32_t path_version; /* the loader requires -1 */
} a2_track;

/* Load etappeN.* from `dir` given a stem such as "etappe0".  Missing optional
 * files are tolerated; a malformed .rmp or .pmp is fatal.  Returns 0 on success. */
int a2_track_load(a2_track *tr, const char *dir, const char *stem);
void a2_track_free(a2_track *tr);

#endif /* A2_TRACK_H */
