/* The new AI model's road: the `.dmp` waypoint graph, the waypoints' obstacle
 * lists, and ScanPath.
 *
 *   0x443da0  the nearest waypoint to an object, on one carriageway
 *   0x4424b0  rceng_add_object_in_waypoint_obstacle_list
 *   0x40bf90  the world mapper's link step (its default mode)
 *   0x409670  ScanPath, a depth-first search five waypoints deep
 *   0x409c10  "a slower car shares my waypoint ahead of me"
 *   0x40c440  a waypoint's index in its chunk's .pmp list
 *   0x442700 / 0x407660  chunk distance and direction
 *
 * The runtime waypoint keeps the .dmp links reordered (loader 0x443c90,
 * constructor 0x4443a0); the directions here are the mapper's:
 * 0 = lane +1, 1 = straight on, 2 = lane -1, 3 = back.  See
 * research/discoveries.md, "The new AI model, traced". */
#ifndef A2_AI_WORLD_H
#define A2_AI_WORLD_H

#include "drive.h"
#include "track.h"

enum { A2_AIW_LANE_UP = 0, A2_AIW_STRAIGHT = 1, A2_AIW_LANE_DOWN = 2, A2_AIW_BACK = 3 };

typedef struct a2_aiw a2_aiw;

a2_aiw *a2_aiw_create(const a2_track *tr);
void a2_aiw_free(a2_aiw *w);

int a2_aiw_chunk_count(const a2_aiw *w);
/* Waypoint position, its chunk and carriageway (1 = the .pmp right-hand
 * list, the player's; 0 = the left).  0 when `wp` is out of range. */
int a2_aiw_wp(const a2_aiw *w, int wp, float pos[3], int *chunk, int *side);
int a2_aiw_link(const a2_aiw *w, int wp, int dir);
/* 0x443da0: the waypoint of `chunk` on `side` (1 right, 0 left, -1 both)
 * nearest (x, y, z), or -1. */
int a2_aiw_nearest(const a2_aiw *w, int chunk, float x, float y, float z, int side);
/* 0x40c440: the waypoint's index in `chunk`'s left list, else its right list,
 * else -1. */
int a2_aiw_lane_index(const a2_aiw *w, int chunk, int wp);
/* The raw length of `chunk`'s side list (+0xe4 side 0, +0xe8 side 1), and
 * 0x442150(chunk, lane, side): entry `lane` of it, clamped to the last; -1
 * when the list is empty. */
int a2_aiw_side_count(const a2_aiw *w, int chunk, int side);
int a2_aiw_side_wp(const a2_aiw *w, int chunk, int lane, int side);
/* The chunk's two .pmp trigger slots. */
int a2_aiw_trigger(const a2_aiw *w, int chunk, int slot);
/* 0x442700: |a - b|, the short way round. */
int a2_aiw_chunk_dist(const a2_aiw *w, int a, int b);
/* 0x407660: 1 when chunk `a` is ahead of `b` (the short way round). */
int a2_aiw_ahead(const a2_aiw *w, int a, int b);
/* The road's direction at `chunk` on `side`, from the first waypoint of the
 * list to its next (0x442150 / 0x4075f0), for 0x410a10. */
int a2_aiw_road_dir(const a2_aiw *w, int chunk, int side, float *dx, float *dz);
/* 0x40a100: 1 when an object heading `heading` (4096 units) at (x, z) drives
 * the way its waypoint `wp` leads, -1 when it drives against it. */
int a2_aiw_with_lane(const a2_aiw *w, int wp, const a2_drive *d);

/* Obstacle lists: cleared and refilled every frame with every vehicle's
 * current waypoint (at most 10 a waypoint, as rd_drmap.cpp). */
void a2_aiw_occ_clear(a2_aiw *w);
void a2_aiw_occ_add(a2_aiw *w, int wp, int id, float x, float z, float speed);
int a2_aiw_occ_count(const a2_aiw *w, int wp);
/* 0x409c10: is a vehicle other than `self_id` on `wp`, in front of `d`
 * (bearing outside 1024..3072) and slower than it? */
int a2_aiw_blocked(const a2_aiw *w, int wp, int self_id, const a2_drive *d);

/* Obstacle-list ids, one per vehicle. */
enum { A2_AI_ID_PLAYER = 1, A2_AI_ID_RACER = 10, A2_AI_ID_COP = 100, A2_AI_ID_TRAFFIC = 1000 };

/* What the AI reads of the player each frame. */
typedef struct {
    float x, y, z;
    const a2_drive *d;  /* heading, v_fwd, v_lat */
    int chunk, wp;      /* the player's chunk and waypoint (0x4d9e44, object +0x20) */
    float progress;     /* chunks driven since the start (0x4d9e70) */
    float progress_abs; /* the same, from the absolute start chunk, as racers count */
    int rank;           /* racers ahead of the player (0x4d9e60) */
    int skid, stopped;  /* 0x4a63dc, 0x4a63e4 */
    int mode;           /* the game mode 0x4d9a7c */
} a2_ai_player;

/* A vehicle a cop can chase. */
typedef struct {
    const a2_drive *d;
    int chunk, wp;
    float limit; /* a racer's record +0x2c */
    int state;   /* a racer's state, -1 for the player */
} a2_ai_target;

/* 0x409670 with depth 5: returns the next target waypoint (-1 when every
 * path is blocked and the fallback found nothing), `near` = the waypoint two
 * ahead and `far` the fourth.  `far` is left as it was when the fallback is
 * taken (the original leaves its local uninitialised).  `order` is the
 * direction preference, {1, 2, 0} normally. */
int a2_aiw_scan_path(const a2_aiw *w, int wp, const int order[3], float near[3], float far[3]);

#endif /* A2_AI_WORLD_H */
