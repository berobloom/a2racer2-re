#include "traffic.h"

#include "ai_car.h"
#include "ai_lane.h"
#include "ai_world.h"
#include "contact.h"
#include "drive.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The traffic vehicles.  `half_len` and `half_wid` are half the mesh's Z and X
 * extent, measured from the `.d3d` bounding boxes (world units, 10 per metre):
 *
 *     n_bus    93.6 x 25.5     n_vrtaut 91.2 x 20.8     n_bestel 53.2 x 20.1
 *     n_calibr 46.8 x 18.2     n_golf   41.4 x 18.9     n_astra  40.3 x 16.0
 *     n_polo   39.1 x 17.8
 *
 * They matter because a bus is 9.4 m long -- more than twice a car, and most of
 * a chunk.  Treating every vehicle as one small circle let the player drive
 * into the middle of a bus, and let the overtake check read a lane as clear
 * when the bus's centre happened to fall between two probe points.
 *
 * The road roller (N_WALS) used to be in this list at 8 m/s.  The original's
 * traffic spawner never picks it -- see SPAWN below -- so it is gone. */
static const struct {
    const char *anim;
    float half_len;
    float half_wid;
} TRAFFIC_KINDS[] = {
    {"N_GOLF_ANIM", 20.7f, 9.5f},   {"N_ASTRA_ANIM", 20.2f, 8.0f},
    {"N_BUS_ANIM", 46.8f, 12.8f},   {"N_VRTAUT_ANIM", 45.6f, 10.4f},
    {"N_POLO_ANIM", 19.6f, 8.9f},   {"N_BESTEL_ANIM", 26.6f, 10.1f},
    {"N_CALIBR_ANIM", 23.4f, 9.1f},
};
/* The original's spawn table, 12 {vehicle, behaviour} pairs at 0x484a48.  The
 * vehicle is an index into the 0x484B18 physics table (9 N_GOLF, 10 N_ASTRA,
 * 11 N_BUS, 12 N_VRTAUT, 13 N_POLO, 14 N_BESTEL, 15 N_CALIBR), here as an
 * index into TRAFFIC_KINDS.  The spawner (0x40cacd..0x40cb04) makes **two**
 * rand() % 12 draws: one picks the vehicle from the first column and the
 * other the behaviour from the second, so a car's speed is not tied to its
 * model -- a bus is as likely as a Golf to drive like behaviour 3. */
static const int SPAWN_KIND[12] = {0, 0, 1, 1, 4, 4, 2, 3, 5, 5, 6, 6};
static const int SPAWN_BEH[12] = {2, 3, 2, 3, 2, 3, 2, 2, 2, 3, 2, 3};
/* The two traffic behaviours, from the emulated init_ai_states (see
 * racers.c): bh_brake_speed, acceleration, bh_preferred_speed, deceleration.
 * Both prefer 25 m/s, the figure a real race's debug.txt logs for every
 * traffic car. */
static const struct {
    float brake, accel, preferred, decel;
    float rear_gap, close, follow; /* +0x1c, +0x20, +0x24, world units */
    int ahead;    /* +0x00: how many waypoints ahead calc_speed looks */
    float reach;  /* +0x04: the arrival test's factor (0x406e10) */
    float turn;   /* +0x0c: the yaw-rate gain (0x406fd0), rad/s */
} TRAFFIC_BEH[2] = {
    {11.5f, 2.55f, 25.0f, 13.0f, 200.0f, 200.0f, 300.0f, 8, 1.0f, 5.890f}, /* behaviour 2 */
    {21.0f, 3.90f, 25.0f, 14.5f, 200.0f, 200.0f, 300.0f, 4, 1.2f, 8.099f}, /* behaviour 3 */
};
#define TRAFFIC_KIND_COUNT ((int)(sizeof TRAFFIC_KINDS / sizeof TRAFFIC_KINDS[0]))

typedef struct {
    int model; /* handle from a2_scene_load_model */
    const char *anim; /* TRAFFIC_KINDS anim name, for its engine sound */
    int beh;   /* 0 = behaviour 2, 1 = behaviour 3 */
    float speed;
    float half_len, half_wid;
    int chunk;
    int lane;
    int lane_from;  /* lane being left during a change; == lane when settled */
    float lane_mix; /* 0..1 blend from lane_from to lane */
    float recheck;  /* unused since the lane choice runs every tick */
    float chosen;   /* ai_chosen_speed (ai +0x1c), m/s */
    float t;        /* 0..1 along the segment from `chunk` to the next */
    float x, y, z, yaw;
    int alive;      /* object +0x34: placed and in play; 0 while hidden */
    int side;       /* 1 the player's carriageway (chunk order), 0 the other */
    /* handle_basic_ai (0x4076b0), with an a2_aiw: the car is an engine
     * object steered at its waypoints */
    a2_drive d;     /* position, heading, v_fwd (+0x74), yaw_rate (+0x7c) */
    int w0;         /* AI record +0: the waypoint it is on */
    int tlane;      /* AI record +0x14: the lane W0 is in, which change_lane
                     * steps one at a time toward the lane it wants */
    int at;         /* vehicle +0x18: the chunk the car is in (0x442020), -1 unset */
} traffic_car;

struct a2_traffic {
    traffic_car *cars;
    int count;
    int models[TRAFFIC_KIND_COUNT];
    const char *anims[TRAFFIC_KIND_COUNT];
    int kind_slot[TRAFFIC_KIND_COUNT]; /* TRAFFIC_KINDS index -> loaded slot, -1 */
    float *limit; /* per-chunk AI speed limit, m/s; NULL = none */
    float *limit0; /* the same for the other carriageway, read against the
                    * chunk order */
    a2_traffic_other others[16];
    int other_count;
    int limit_count;
    float half_lens[TRAFFIC_KIND_COUNT];
    float half_wids[TRAFFIC_KIND_COUNT];
    int model_count;
    unsigned seed;
    int lane_changes; /* diagnostic: how often traffic has pulled over */
    int vchunks;      /* Game.ini visiblechunks, [0x4d9a8c] */
    int driven;       /* the cars are engine objects (handle_basic_ai) */
    int placed;       /* diagnostic: placements by the recycler */
};

static unsigned next_rand(unsigned *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) & 0x7FFF;
}

/* The chunk after `chunk` in the car's direction of travel, or -1 off the
 * end of a stage. */
static int next_chunk(const a2_scene *scene, int side, int chunk)
{
    return a2_scene_wrap_chunk(scene, chunk + (side ? 1 : -1));
}

static int lanes_at(const a2_scene *scene, int side, int chunk)
{
    return chunk < 0 ? 0 : a2_scene_lane_count_side(scene, chunk, side);
}

/* Lane `lane` of `chunk` on the car's carriageway, clamped to the lanes
 * there. */
static int lane_pt(const a2_scene *scene, int side, int chunk, int lane, float out[3])
{
    int n = lanes_at(scene, side, chunk);
    if (n <= 0)
        return 0;
    return a2_scene_lane_point_side(scene, chunk, lane < n ? lane : n - 1, side, out);
}

/* Position on one lane's segment at `c->t`. */
static int lane_point_at(const traffic_car *c, const a2_scene *scene, int lane, float out[3])
{
    float a[3], b[3];
    if (!lane_pt(scene, c->side, c->chunk, lane, a) ||
        !lane_pt(scene, c->side, next_chunk(scene, c->side, c->chunk), lane, b))
        return 0;
    out[0] = a[0] + (b[0] - a[0]) * c->t;
    out[1] = a[1] + (b[1] - a[1]) * c->t;
    out[2] = a[2] + (b[2] - a[2]) * c->t;
    return 1;
}

/* Place a car at (chunk, lane, t) and work out its facing from the segment. */
/* HANDLE_TRAFFIC_AI's last step (0x40b952): y becomes the plane height of
 * the car's chunk, which 0x40ede0 returns whether or not it clamped. */
static void ground_car(traffic_car *c, const a2_scene *scene)
{
    float gy;
    c->at = a2_scene_track_chunk(scene, c->at >= 0 ? c->at : c->chunk, c->x, c->z);
    if (a2_scene_chunk_height(scene, c->at, c->x, c->z, &gy))
        c->y = gy;
}

static int place(traffic_car *c, const a2_scene *scene)
{
    float a[3], b[3];
    if (!lane_pt(scene, c->side, c->chunk, c->lane, a) ||
        !lane_pt(scene, c->side, next_chunk(scene, c->side, c->chunk), c->lane, b))
        return 0;

    c->x = a[0] + (b[0] - a[0]) * c->t;
    c->y = a[1] + (b[1] - a[1]) * c->t;
    c->z = a[2] + (b[2] - a[2]) * c->t;
    /* Mid lane-change: slide across from the lane being left, so the car moves
     * over rather than snapping a whole lane width in one frame. */
    if (c->lane_mix < 1.0f && c->lane_from != c->lane) {
        float from[3];
        if (lane_point_at(c, scene, c->lane_from, from)) {
            float k = 1.0f - c->lane_mix;
            c->x += (from[0] - c->x) * k;
            c->y += (from[1] - c->y) * k;
            c->z += (from[2] - c->z) * k;
        }
    }
    float dx = b[0] - a[0], dz = b[2] - a[2];
    if (dx * dx + dz * dz > 1e-6f)
        c->yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
    ground_car(c, scene);
    return 1;
}

/* Nearest point on a traffic car's body axis to (x, z).  A vehicle is treated
 * as a capsule: a segment of length 2*half_len along its facing, not a point.
 * For a 9.4 m bus that is the difference between a usable collision and driving
 * through the middle of it. */
static void body_axis_nearest(const traffic_car *c, float x, float z, float *ox, float *oz)
{
    float r = c->yaw * (float)M_PI / 180.0f;
    float fx = sinf(r), fz = -cosf(r);
    float along = (x - c->x) * fx + (z - c->z) * fz;
    if (along > c->half_len)
        along = c->half_len;
    else if (along < -c->half_len)
        along = -c->half_len;
    *ox = c->x + fx * along;
    *oz = c->z + fz * along;
}

#define LANE_CHANGE_SECS 1.2f

/* ---- ai_lane.cpp, the traffic arm (session 13) ----------------------------
 *
 * change_lane (0x403660) runs every tick.  For each lane of the chunk it
 * fills a t_lane_info: the nearest vehicle on the first waypoint ahead
 * holding one, scanning up to 10 waypoints (ai +0x10 = 10, 0x406a13), and
 * the same behind over 2 (scan_lane 0x403f60, find_obstacle 0x404600: the
 * 3D distance is the range).  Its status (0x404720): 0 none, 1 faster than
 * us, 2 not faster, 5 oncoming; 3 and 4 are dead ends, which the port's
 * lanes do not have.  The traffic chooser (0x404960) then picks the lane and
 * 0x4055c0 the speed.  Lane index n-1 is the slowest lane (LIKELY the same
 * order as the port's lane points, which come from the same .dmp lists). */
typedef a2_lane_info lane_info;
typedef a2_ai_vehicle seen;

static int gather(const a2_traffic *t, const a2_scene *scene, seen *out, int max)
{
    int n = 0;
    for (int i = 0; i < t->count && n < max; i++) {
        const traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;
        out[n++] = (seen){c->x, c->y, c->z, c->speed, c->chunk, c->lane, c->t, !c->side};
    }
    for (int i = 0; i < t->other_count && n < max; i++) {
        const a2_traffic_other *o = &t->others[i];
        int ch = a2_scene_nearest_chunk(scene, o->x, o->z);
        if (ch < 0)
            continue;
        /* on whichever carriageway its nearest waypoint is (0x443da0 with
         * both lists) */
        float d1 = 3.4e38f, d0 = 3.4e38f;
        int l1 = a2_scene_nearest_lane_side(scene, ch, 1, o->x, o->z, &d1);
        int l0 = a2_scene_nearest_lane_side(scene, ch, 0, o->x, o->z, &d0);
        int on0 = l0 >= 0 && (l1 < 0 || d0 < d1);
        int ln = on0 ? l0 : l1;
        if (ln < 0)
            continue;
        out[n++] = (seen){o->x, o->y, o->z, o->speed, ch, ln, 0.5f, on0};
    }
    return n;
}

/* the tests the chooser repeats: room behind (rear_status 0, or the one
 * behind further than +0x1c) and room ahead (the one ahead at least +0x24
 * away) */
static int rear_ok(const lane_info *l, int beh)
{
    return l->rear == 0 || TRAFFIC_BEH[beh].rear_gap < l->rear_range;
}
static int front_ok(const lane_info *l, int beh) { return !(TRAFFIC_BEH[beh].follow > l->front_range); }

/* PICK_LANE, 0x404960, for a car not in an "illegal" lane */
static int pick_lane(const lane_info *L, int lanes, int c, int beh, float my_speed)
{
    if (c == lanes - 1) { /* in the slowest lane */
        int s = L[c].front;
        if (s == 0 || s == 1)
            return c; /* free / faster: staying */
        if (c == 0)
            return 0;
        const lane_info *l = &L[c - 1];
        if (s == 2) { /* slower: checking (table 0x404e80) */
            if (l->front == 0)
                return rear_ok(l, beh) ? c - 1 : c;
            if (l->front == 1)
                return front_ok(l, beh) && rear_ok(l, beh) ? c - 1 : c;
            if (l->front == 2)
                return l->front_speed > my_speed && front_ok(l, beh) && rear_ok(l, beh) ? c - 1 : c;
            return c;
        }
        /* brake, stop or ghost: switching left (table 0x404e98) */
        if (l->front == 0)
            return rear_ok(l, beh) ? c - 1 : c;
        if (l->front == 1 || l->front == 2)
            return front_ok(l, beh) && rear_ok(l, beh) ? c - 1 : c;
        return c;
    }
    /* not in the slowest lane (0x404d33) */
    int s = L[c].front;
    if (s >= 2 && c - 1 >= 0) { /* blocked: left first (0x404d51) */
        const lane_info *l = &L[c - 1];
        if (l->front == 0 && rear_ok(l, beh))
            return c - 1;
        if (l->front == 1 && front_ok(l, beh) && rear_ok(l, beh))
            return c - 1;
    }
    /* then back to the right (0x404db5, table 0x404ec8) */
    const lane_info *r = &L[c + 1];
    if (r->front == 0)
        return rear_ok(r, beh) ? c + 1 : c;
    if (r->front == 1)
        return front_ok(r, beh) && rear_ok(r, beh) ? c + 1 : c;
    return c;
}

static float lane_speed(const lane_info *l, int beh, float speed)
{
    return a2_ai_lane_speed(l, TRAFFIC_BEH[beh].preferred, TRAFFIC_BEH[beh].close,
                            TRAFFIC_BEH[beh].follow, speed);
}

void a2_traffic_set_others(a2_traffic *t, const a2_traffic_other *o, int n)
{
    if (!t)
        return;
    if (n > 16)
        n = 16;
    memcpy(t->others, o, sizeof *o * (size_t)(n > 0 ? n : 0));
    t->other_count = n > 0 ? n : 0;
}

/* 0x4426e0: a chunk index brought into the track, once round. */
static int wrap_chunk_4426e0(int c, int n)
{
    if (c >= n)
        c -= n;
    if (c < 0)
        c = n + c;
    return c;
}

/* One car of the traffic spawner (0x40ca70): two rand() % 12 draws into the
 * table, the vehicle and the behaviour, then the car is made at `chunk`, lane
 * 0 of its carriageway, and left out of play (+0x34 = 0) for the recycler to
 * bring in. */
static void spawn(a2_traffic *t, traffic_car *c, int chunk, int side)
{
    int kind = -1;
    for (int tries = 0; tries < 12 && kind < 0; tries++)
        kind = t->kind_slot[SPAWN_KIND[next_rand(&t->seed) % 12u]];
    if (kind < 0)
        kind = 0;
    c->model = t->models[kind];
    c->anim = t->anims[kind];
    c->beh = SPAWN_BEH[next_rand(&t->seed) % 12u] == 3 ? 1 : 0;
    c->speed = TRAFFIC_BEH[c->beh].preferred;
    c->chosen = c->speed;
    c->half_len = t->half_lens[kind];
    c->half_wid = t->half_wids[kind];
    c->chunk = chunk;
    c->at = -1;
    c->side = side;
    c->lane = 0;
    c->t = 0.0f;
    c->lane_from = c->lane;
    c->lane_mix = 1.0f;
    c->alive = 0;
}

/* The spawner's two groups (0x40ca70): [0x484aa8] cars on side
 * `DIRECTION == 0` (the player's) and [0x484aac] on the other, each count / 2
 * (truncated) at STARTCHUNK - vchunks - 5 - 3i (behind) and as many at
 * STARTCHUNK + vchunks + 5 + 3i (ahead).  All start out of play, outside the
 * window.  The file holds 8 and 12, but init (0x423df7) overwrites them by
 * Difficulty: 10 and 14 on HARD, 5 and 7 on EASY. */
a2_traffic *a2_traffic_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                              int start_chunk, int vchunks, int difficulty)
{
    const int own_side = difficulty ? 10 : 5, other_side = difficulty ? 14 : 7;
    a2_traffic *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->seed = 12345u;
    t->vchunks = vchunks;

    for (int i = 0; i < TRAFFIC_KIND_COUNT; i++) {
        t->kind_slot[i] = -1;
        int m = a2_scene_load_model(scene, rcs, anims, TRAFFIC_KINDS[i].anim);
        if (m >= 0) {
            t->kind_slot[i] = t->model_count;
            t->models[t->model_count] = m;
            t->anims[t->model_count] = TRAFFIC_KINDS[i].anim;
            t->half_lens[t->model_count] = TRAFFIC_KINDS[i].half_len;
            t->half_wids[t->model_count] = TRAFFIC_KINDS[i].half_wid;
            t->model_count++;
        }
    }
    if (!t->model_count) {
        fprintf(stderr, "traffic: no models could be loaded\n");
        free(t);
        return NULL;
    }

    int count = own_side / 2 * 2 + other_side / 2 * 2;
    t->cars = calloc((size_t)count, sizeof(traffic_car));
    if (!t->cars) {
        free(t);
        return NULL;
    }
    t->count = count;
    int n = a2_scene_chunk_count(scene), k = 0;
    for (int g = 0; g < 2; g++) {
        int side = g == 0 ? 1 : 0, half = (g == 0 ? own_side : other_side) / 2;
        for (int i = 0; i < half; i++)
            spawn(t, &t->cars[k++], wrap_chunk_4426e0(start_chunk - vchunks - 5 - 3 * i, n),
                  side);
        for (int i = 0; i < half; i++)
            spawn(t, &t->cars[k++], wrap_chunk_4426e0(start_chunk + vchunks + 5 + 3 * i, n),
                  side);
    }
    fprintf(stderr, "traffic: %d cars (%d on the player's side, %s), %d models\n", count,
            own_side / 2 * 2, difficulty ? "HARD" : "EASY", t->model_count);
    return t;
}

void a2_traffic_free(a2_traffic *t)
{
    if (!t)
        return;
    free(t->cars);
    free(t->limit);
    free(t->limit0);
    free(t);
}

/* the AI's reading of one speed trigger (0x407ba0), or `cur` for none */
static float trigger_limit(uint32_t trig, float cur)
{
    switch (trig) {
    case 4: return 16.67f;
    case 5: return 22.22f;
    case 6: return 33.33f;
    case 8: return 500.0f;
    default: return cur;
    }
}

void a2_traffic_set_limits(a2_traffic *t, const a2_track *track)
{
    /* The AI's own reading of the speed triggers (0x407ba0), which is not the
     * police's or the speed camera's: 4 -> 60 km/h, 5 -> 80, 6 -> 120, 8 -> no
     * limit, 10 -> stop (not used by the shipped tracks), 7 and 9 nothing.  A
     * car takes the limit as it enters the chunk and keeps it until the next
     * trigger, so this is the limit in force on each chunk for a car that has
     * driven there from the start -- in chunk order on the player's side, and
     * against it on the other. */
    if (!t || !track)
        return;
    int n = (int)track->pmp.chunk_count;
    free(t->limit);
    free(t->limit0);
    t->limit = calloc(n ? (size_t)n : 1, sizeof(float));
    t->limit0 = calloc(n ? (size_t)n : 1, sizeof(float));
    if (!t->limit || !t->limit0) {
        free(t->limit);
        free(t->limit0);
        t->limit = t->limit0 = NULL;
        return;
    }
    t->limit_count = n;
    float cur = 500.0f;
    for (int i = 0; i < n; i++) {
        cur = trigger_limit(track->pmp.chunks[i].trigger0, cur);
        cur = trigger_limit(track->pmp.chunks[i].trigger1, cur);
        t->limit[i] = cur;
    }
    cur = 500.0f;
    for (int i = n - 1; i >= 0; i--) {
        cur = trigger_limit(track->pmp.chunks[i].trigger0, cur);
        cur = trigger_limit(track->pmp.chunks[i].trigger1, cur);
        t->limit0[i] = cur;
    }
}

/* ---- handle_basic_ai (0x4076b0), the driving of a traffic car -----------
 *
 * Not a vehicle model: the car is an engine object whose velocity and yaw
 * rate the AI sets every frame, and the engine (0x440fb0) moves it, stopping
 * at contacts, as it moves every object.  Per frame:
 *   - W1 = the waypoint after the one it is on (W0, record +0) and `far`
 *     the one behaviour+0 waypoints on (0x4075f0 each time);
 *   - calc_speed (0x407140) toward `far`; the lateral velocity is zeroed;
 *   - the yaw rate (0x406fd0) = behaviour+0xc * (f x d) / |d|, d from the car
 *     to W1, f its forward axis; zero below 1.5 m/s;
 *   - W0 becomes W1 once the car is within behaviour+4 * |W1 - W0| * k of W1,
 *     k = 1 + (8 - min(|128 - |W1 - W0||, 8)) / 16 (0x406e10; checked under
 *     Unicorn, reverse-engineering/tools/emu_traffic.py).
 * See research/discoveries.md, "Traffic driving: handle_basic_ai". */

/* 0x4075f0: the straight link, else at a lane's end lane +1 or lane -1 by the
 * parity of a timer reading (here a draw), else the waypoint itself. */
static int wp_step(a2_traffic *t, const struct a2_aiw *aiw, int wp)
{
    int n = a2_aiw_link(aiw, wp, A2_AIW_STRAIGHT);
    if (n >= 0)
        return n;
    int odd = next_rand(&t->seed) & 1;
    int a = a2_aiw_link(aiw, wp, odd ? A2_AIW_LANE_UP : A2_AIW_LANE_DOWN);
    int b = a2_aiw_link(aiw, wp, odd ? A2_AIW_LANE_DOWN : A2_AIW_LANE_UP);
    return a >= 0 ? a : b >= 0 ? b : wp;
}

static int wp_pos(const struct a2_aiw *aiw, int wp, float p[3], int *chunk, int *side)
{
    int ch = -1, sd = 1;
    if (wp < 0 || !a2_aiw_wp(aiw, wp, p, &ch, &sd))
        return 0;
    if (chunk)
        *chunk = ch;
    if (side)
        *side = sd;
    return 1;
}

/* the car's forward axis, (-sin a, cos a) in the engine's convention */
static void drive_fwd(const a2_drive *d, float *fx, float *fz)
{
    float a = d->heading * (6.28318531f / 4096.0f);
    *fx = -sinf(a);
    *fz = cosf(a);
}

/* What the lane scans, the draw and the collisions read: the chunk and lane
 * of W0, the position, and how far along W0 -> W1 the car is. */
static void sync_car(a2_traffic *t, traffic_car *c, const a2_scene *scene,
                     const struct a2_aiw *aiw)
{
    float p0[3], p1[3];
    int ch, sd;
    c->x = c->d.x;
    c->z = c->d.z;
    c->speed = c->d.v_fwd;
    c->yaw = 180.0f + c->d.heading * (360.0f / 4096.0f);
    if (!wp_pos(aiw, c->w0, p0, &ch, &sd))
        return;
    c->chunk = ch;
    /* object +0x20, the waypoint nearest the car (0x4424b0 / 0x443da0),
     * gives the lane change_lane starts from; W0's lane is the target */
    int ln = a2_scene_nearest_lane_side(scene, ch, c->side, c->x, c->z, NULL);
    if (ln >= 0)
        c->lane = ln;
    int tl = a2_scene_nearest_lane_side(scene, ch, c->side, p0[0], p0[2], NULL);
    if (tl >= 0)
        c->tlane = tl;
    c->lane_from = c->lane;
    c->lane_mix = 1.0f;
    c->t = 0.0f;
    int w1 = a2_aiw_link(aiw, c->w0, A2_AIW_STRAIGHT);
    if (w1 >= 0 && wp_pos(aiw, w1, p1, NULL, NULL)) {
        float sx = p1[0] - p0[0], sz = p1[2] - p0[2], l2 = sx * sx + sz * sz;
        if (l2 > 1e-3f) {
            float u = ((c->x - p0[0]) * sx + (c->z - p0[2]) * sz) / l2;
            c->t = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        }
    }
    ground_car(c, scene);
    c->d.y = c->y;
    (void)t;
}

/* One frame of handle_basic_ai after change_lane; 0 when the car ran out of
 * road. */
static int basic_ai(a2_traffic *t, traffic_car *c, const struct a2_aiw *aiw, float allowed,
                    float dt)
{
    const float pref = TRAFFIC_BEH[c->beh].preferred;
    int w1 = wp_step(t, aiw, c->w0);
    int far = c->w0;
    for (int k = 0; k < TRAFFIC_BEH[c->beh].ahead; k++)
        far = wp_step(t, aiw, far);
    if (w1 == c->w0)
        return 0; /* the end of the stage */
    float p0[3], p1[3], pf[3];
    if (!wp_pos(aiw, c->w0, p0, NULL, NULL) || !wp_pos(aiw, w1, p1, NULL, NULL) ||
        !wp_pos(aiw, far, pf, NULL, NULL))
        return 0;
    float fx, fz;
    drive_fwd(&c->d, &fx, &fz);
    /* 0x4078f5: a speed over four times the preferred is reset to it */
    if (c->d.v_fwd > 4.0f * pref)
        c->d.v_fwd = pref;
    c->d.v_lat = 0.0f;
    /* calc_speed: the chosen speed less brake * |sin| of the angle to `far` */
    {
        float dx = pf[0] - c->d.x, dz = pf[2] - c->d.z, l = sqrtf(dx * dx + dz * dz);
        float sin_a = l > 1e-3f ? fabsf(fx * dz - fz * dx) / l : 0.0f;
        float desired = c->chosen - TRAFFIC_BEH[c->beh].brake * sin_a * c->chosen / pref;
        if (desired > allowed)
            desired = allowed;
        float v = c->d.v_fwd;
        if (desired > v) {
            v += TRAFFIC_BEH[c->beh].accel * dt;
            if (v > desired)
                v = desired;
        } else if (desired < v) {
            v -= TRAFFIC_BEH[c->beh].decel * dt;
            if (v < desired)
                v = desired;
            if (v < 0.0f)
                v = 0.0f;
        }
        c->d.v_fwd = v;
    }
    /* 0x406fd0: the yaw rate toward W1 */
    {
        float dx = p1[0] - c->d.x, dy = p1[1] - c->d.y, dz = p1[2] - c->d.z;
        float l = sqrtf(dx * dx + dy * dy + dz * dz);
        c->d.yaw_rate = l > 1e-3f ? TRAFFIC_BEH[c->beh].turn * (fx * dz - fz * dx) / l : 0.0f;
        if (c->d.v_fwd < 1.5f)
            c->d.yaw_rate = 0.0f;
    }
    /* 0x406e10: on to the next waypoint */
    {
        float sx = p1[0] - p0[0], sy = p1[1] - p0[1], sz = p1[2] - p0[2];
        float seg = sqrtf(sx * sx + sy * sy + sz * sz);
        float k = fabsf(128.0f - seg);
        if (k > 8.0f)
            k = 8.0f;
        float reach = TRAFFIC_BEH[c->beh].reach * seg * (1.0f + (8.0f - k) / 16.0f);
        float dx = p1[0] - c->d.x, dy = p1[1] - c->d.y, dz = p1[2] - c->d.z;
        if (sqrtf(dx * dx + dy * dy + dz * dz) < reach)
            c->w0 = w1;
    }
    return 1;
}

/* 0x40b650: may a car be put on waypoint `wp` at `pos`?  The waypoint and
 * its next and previous waypoints must have empty obstacle lists, and no car
 * in play may overlap it (0x446a10).  The original tests the two links for
 * -1 only on its second look, after reading through them (open question 55);
 * the port skips a missing link, which is what that second look intends. */
static int wp_free(const a2_traffic *t, const struct a2_aiw *aiw, int wp, const float pos[3],
                   const traffic_car *me)
{
    if (aiw) {
        if (a2_aiw_occ_count(aiw, wp) > 0)
            return 0;
        int next = a2_aiw_link(aiw, wp, A2_AIW_STRAIGHT);
        int prev = a2_aiw_link(aiw, wp, A2_AIW_BACK);
        if (next >= 0 && a2_aiw_occ_count(aiw, next) > 0)
            return 0;
        if (prev >= 0 && a2_aiw_occ_count(aiw, prev) > 0)
            return 0;
    }
    /* LIKELY close to 0x446a10, which compares the two objects' half sizes
     * (+0x6c, +0x74); here the centres against the sum of half lengths */
    for (int i = 0; i < t->count; i++) {
        const traffic_car *o = &t->cars[i];
        if (o == me || !o->alive)
            continue;
        float dx = o->x - pos[0], dz = o->z - pos[2], r = o->half_len + me->half_len;
        if (dx * dx + dz * dz < r * r)
            return 0;
    }
    for (int i = 0; i < t->other_count; i++) {
        float dx = t->others[i].x - pos[0], dz = t->others[i].z - pos[2];
        float r = 25.0f + me->half_len;
        if (dx * dx + dz * dz < r * r)
            return 0;
    }
    return 1;
}

/* 0x40b480(car, chunk, 0): put the car on lane 0 of its carriageway at
 * `chunk` (0x442150), or take it out of play (+0x34 = 0) when that is not
 * possible this frame. */
static void put_at(a2_traffic *t, traffic_car *c, const a2_scene *scene, const struct a2_aiw *aiw,
                   float dt, int chunk)
{
    int n = a2_scene_chunk_count(scene);
    chunk = wrap_chunk_4426e0(chunk, n);
    c->alive = 0;
    /* only on a chunk where the player's carriageway has more than one lane
     * (+0xe8 with DIRECTION 0, whichever side the car is on) */
    if (aiw && a2_aiw_side_count(aiw, chunk, 1) == 1)
        return;
    /* 0x40b650 in mode 0 goes on one call in 30 (rand() % 30); the port
     * makes that a chance of dt a frame, the same at 30 fps.  It also refuses
     * below 14.5 fps (1 / frame time < 14.5), which the port leaves out. */
    if ((float)(next_rand(&t->seed) % 10000u) >= dt * 10000.0f)
        return;
    float pos[3];
    int lane = 0, wp = -1;
    if (aiw) {
        wp = a2_aiw_side_wp(aiw, chunk, 0, c->side);
        int wch, wside;
        if (wp < 0 || !a2_aiw_wp(aiw, wp, pos, &wch, &wside) || !wp_free(t, aiw, wp, pos, c))
            return;
        lane = a2_scene_nearest_lane_side(scene, chunk, c->side, pos[0], pos[2], NULL);
    } else {
        if (!lane_pt(scene, c->side, chunk, 0, pos) || !wp_free(t, NULL, -1, pos, c))
            return;
    }
    if (lane < 0)
        return;
    c->chunk = chunk;
    c->at = -1;
    c->lane = c->lane_from = lane;
    c->lane_mix = 1.0f;
    c->t = 0.0f;
    if (aiw) {
        /* 0x4420f0 / 0x442230: on the waypoint, facing along its lane; the
         * speed is the car's own (the AI record keeps it) */
        float v = c->speed, p1[3];
        memset(&c->d, 0, sizeof c->d);
        c->d.x = pos[0];
        c->d.y = pos[1];
        c->d.z = pos[2];
        int w1 = a2_aiw_link(aiw, wp, A2_AIW_STRAIGHT);
        if (w1 >= 0 && wp_pos(aiw, w1, p1, NULL, NULL)) {
            /* forward (-sin a, cos a) along W0 -> W1 */
            float a = atan2f(-(p1[0] - pos[0]), p1[2] - pos[2]);
            c->d.heading = a * (4096.0f / 6.28318531f);
            if (c->d.heading < 0.0f)
                c->d.heading += 4096.0f;
        }
        c->d.v_fwd = v;
        c->w0 = wp;
        c->alive = 1;
        sync_car(t, c, scene, aiw);
    } else {
        c->alive = place(c, scene);
    }
    if (c->alive) {
        t->placed++;
        if (getenv("A2_TRAFFIC_LOG"))
            fprintf(stderr, "traffic: car %d placed on side %d chunk %d lane %d\n",
                    (int)(c - t->cars), c->side, chunk, lane);
    }
}

/* ai_check_cars_outside_handling_range, 0x40b2d0, once a frame after the
 * objects have moved.  The window is the player's chunk +- (vchunks - 1)
 * (0x445d70); where it runs past either end of the track it is split in two
 * and the half-track mark tells which part a car is beyond.  A car behind
 * the window goes to its front edge, one ahead of it to its back edge, every
 * frame until it is placed.  (Racers are never moved; cops in state 1 and
 * kind-5 cars have their own handlers, 0x40c640 / 0x40c5f0, not traffic.) */
static void recycle(a2_traffic *t, const a2_scene *scene, const struct a2_aiw *aiw, float dt,
                    int player_chunk)
{
    int n = a2_scene_chunk_count(scene);
    if (n <= 0 || player_chunk < 0)
        return;
    int r = t->vchunks - 1;
    int lo = player_chunk - r, hi = player_chunk + r, hi2 = 0, wrapped = 0;
    if (lo < 0) {
        hi2 = hi % n;
        lo += n - 1;
        hi = n - 1;
        wrapped = 1;
    } else if (hi > n - 1) {
        hi2 = hi % n;
        hi = n - 1;
        wrapped = 1;
    }
    int half = n / 2;
    for (int i = 0; i < t->count; i++) {
        traffic_car *c = &t->cars[i];
        int ch = c->chunk, to;
        if (wrapped) {
            if (ch < half) {
                if (ch <= hi2)
                    continue;
                to = lo;
            } else {
                if (ch >= lo)
                    continue;
                to = hi2;
            }
        } else if (ch < lo) {
            to = hi;
        } else if (ch > hi) {
            to = lo;
        } else {
            continue;
        }
        put_at(t, c, scene, aiw, dt, to);
    }
}

static float dev_worst, dev_sum; /* A2_TRAFFIC_DEV */
static int dev_n, dev_frames;

void a2_traffic_update(a2_traffic *t, const a2_scene *scene, const struct a2_aiw *aiw, float dt,
                       int player_chunk)
{
    if (!t)
        return;
    t->driven = aiw != NULL;
    const float UNITS_PER_METRE = 10.0f;
    static seen vis[256];
    int nvis = gather(t, scene, vis, 256);
    for (int i = 0; i < t->count; i++) {
        traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;

        if (aiw) {
            /* HANDLE_TRAFFIC_AI (0x40b770): change_lane, then handle_basic_ai,
             * then the engine's move (0x4404a0 here; the contact pass in
             * main.c moves it back to its first contact) */
            int lanes = lanes_at(scene, c->side, c->chunk);
            if (lanes > 0 && c->lane < lanes) {
                lane_info L[8];
                if (lanes > 8)
                    lanes = 8;
                seen self = {c->x, c->y, c->z, c->speed, c->chunk, c->lane, c->t, !c->side};
                a2_ai_scan_lanes(&self, vis, nvis, lanes, L);
                int want = lanes > 1 ? pick_lane(L, lanes, c->lane, c->beh, c->speed) : c->lane;
                if (want < 0)
                    want = 0;
                if (want >= lanes)
                    want = lanes - 1;
                /* 0x403c36: against the target lane +0x14, one lane a tick:
                 * 0x406d90 / 0x406d10 move W0 to the next lane's waypoint in
                 * its chunk and step +0x14 */
                int to = want < c->tlane ? c->tlane - 1 : want > c->tlane ? c->tlane + 1 : -1;
                int nw = to >= 0 ? a2_scene_lane_wp_side(scene, c->chunk, to, c->side) : -1;
                if (nw >= 0 && nw != c->w0) {
                    /* while changing: the mean of both lanes (0x403e38) */
                    c->chosen = 0.5f * (lane_speed(&L[c->lane], c->beh, c->chosen) +
                                        lane_speed(&L[want], c->beh, c->chosen));
                    c->w0 = nw;
                    c->tlane = to;
                    t->lane_changes++;
                } else {
                    c->chosen = lane_speed(&L[c->lane], c->beh, c->chosen);
                }
            }
            float allowed = 500.0f;
            const float *lim = c->side ? t->limit : t->limit0;
            if (lim && c->chunk >= 0 && c->chunk < t->limit_count)
                allowed = lim[c->chunk];
            if (!basic_ai(t, c, aiw, allowed, dt)) {
                c->alive = 0;
                continue;
            }
            a2_drive_integrate(&c->d, dt);
            sync_car(t, c, scene, aiw);
            if (getenv("A2_TRAFFIC_DEV")) {
                /* debug: how far the car is off its W0 -> W1 line */
                float p0[3], p1[3];
                int w1 = a2_aiw_link(aiw, c->w0, A2_AIW_STRAIGHT);
                if (w1 >= 0 && wp_pos(aiw, c->w0, p0, NULL, NULL) &&
                    wp_pos(aiw, w1, p1, NULL, NULL)) {
                    float sx = p1[0] - p0[0], sz = p1[2] - p0[2], l = sqrtf(sx * sx + sz * sz);
                    if (l > 1e-3f) {
                        float dev = fabsf((c->x - p0[0]) * sz - (c->z - p0[2]) * sx) / l;
                        dev_worst = fmaxf(dev_worst, dev);
                        dev_sum += dev;
                        dev_n++;
                    }
                }
            }
            continue;
        }

        /* Advance along the current segment, measuring it in world units so a
         * speed in m/s means the same thing everywhere. */
        float a[3], b[3];
        int nc = next_chunk(scene, c->side, c->chunk);
        if (!lane_pt(scene, c->side, c->chunk, c->lane, a) ||
            !lane_pt(scene, c->side, nc, c->lane, b)) {
            c->alive = 0;
            continue;
        }
        float dx = b[0] - a[0], dz = b[2] - a[2];
        float seg = sqrtf(dx * dx + dz * dz);
        if (seg < 1e-3f)
            seg = 1.0f;

        /* Speed, by the original's calc_speed (0x407140) -- the same
         * controller the racers use (see racers.c): the behaviour's preferred
         * speed less brake * |sin| of the angle to the road two chunks ahead
         * (that lookahead is ours), clipped to the speed limit in force, then
         * approached at the behaviour's accel/decel. */
        {
            float allowed = 500.0f;
            const float *lim = c->side ? t->limit : t->limit0;
            if (lim && c->chunk >= 0 && c->chunk < t->limit_count)
                allowed = lim[c->chunk];
            float sin_a = 0.0f;
            float p2[3];
            if (lane_pt(scene, c->side, next_chunk(scene, c->side, nc), c->lane, p2)) {
                float tx = p2[0] - c->x, tz = p2[2] - c->z;
                float tl = sqrtf(tx * tx + tz * tz);
                if (tl > 1e-3f)
                    sin_a = fabsf((dx * tz - dz * tx) / (seg * tl));
            }
            /* calc_speed: chosen - brake * |cross| * chosen / preferred */
            float desired = c->chosen - TRAFFIC_BEH[c->beh].brake * sin_a * c->chosen /
                                            TRAFFIC_BEH[c->beh].preferred;
            if (desired > allowed)
                desired = allowed;
            if (desired > c->speed) {
                c->speed += TRAFFIC_BEH[c->beh].accel * dt;
                if (c->speed > desired)
                    c->speed = desired;
            } else if (desired < c->speed) {
                c->speed -= TRAFFIC_BEH[c->beh].decel * dt;
                if (c->speed < desired)
                    c->speed = desired;
                if (c->speed < 0.0f)
                    c->speed = 0.0f;
            }
        }

        /* Lane and speed choice, every tick, as change_lane does (called
         * from HANDLE_TRAFFIC_AI 0x40b8e7).  A change starts the port's
         * 1.2 s slide; the original retargets the next waypoint (0x406d90 /
         * 0x406d10) and steers there.  While sliding the car keeps its
         * lane. */
        c->lane_mix += dt / LANE_CHANGE_SECS;
        if (c->lane_mix > 1.0f)
            c->lane_mix = 1.0f;
        {
            int lanes = lanes_at(scene, c->side, c->chunk);
            if (lanes > 0 && c->lane < lanes) {
                lane_info L[8];
                if (lanes > 8)
                    lanes = 8;
                seen self = {c->x, c->y, c->z, c->speed, c->chunk, c->lane, c->t, !c->side};
                a2_ai_scan_lanes(&self, vis, nvis, lanes, L);
                int want = c->lane;
                if (lanes > 1 && c->lane_mix >= 1.0f)
                    want = pick_lane(L, lanes, c->lane, c->beh, c->speed);
                if (want < 0)
                    want = 0;
                if (want >= lanes)
                    want = lanes - 1;
                if (want != c->lane) {
                    /* while changing: the mean of both lanes (0x403e38) */
                    c->chosen = 0.5f * (lane_speed(&L[c->lane], c->beh, c->chosen) +
                                        lane_speed(&L[want], c->beh, c->chosen));
                    if (getenv("A2_TRAFFIC_LOG"))
                        fprintf(stderr, "lane: car %d chunk %d %d->%d of %d (front %d %.0f)\n", i,
                                c->chunk, c->lane, want, lanes, L[c->lane].front,
                                L[c->lane].front_range);
                    c->lane_from = c->lane;
                    c->lane = want;
                    c->lane_mix = 0.0f;
                    t->lane_changes++;
                } else {
                    c->chosen = lane_speed(&L[c->lane], c->beh, c->chosen);
                }
            }
        }

        c->t += c->speed * UNITS_PER_METRE * dt / seg;
        while (c->t >= 1.0f) {
            c->t -= 1.0f;
            int to = next_chunk(scene, c->side, c->chunk);
            int ln = lanes_at(scene, c->side, to);
            if (ln <= 0) {
                /* off the end of the stage: out of play until the recycler
                 * brings it back */
                c->alive = 0;
                break;
            }
            c->chunk = to;
            if (c->lane >= ln)
                c->lane = ln - 1;
        }
        if (!c->alive)
            continue;
        if (!place(c, scene))
            c->alive = 0;
    }
    recycle(t, scene, aiw, dt, player_chunk);
    if (getenv("A2_TRAFFIC_DEV") && ++dev_frames % 150 == 0 && dev_n) {
        fprintf(stderr, "trafficdev: mean %.1f worst %.1f units over %d\n", dev_sum / dev_n,
                dev_worst, dev_n);
        dev_worst = dev_sum = 0.0f;
        dev_n = 0;
    }
}

void a2_traffic_draw(const a2_traffic *t, const a2_scene *scene, int player_chunk, int radius)
{
    if (!t)
        return;
    for (int i = 0; i < t->count; i++) {
        const traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;
        int d = a2_scene_chunk_delta(scene, player_chunk, c->chunk);
        if (d < -radius || d > radius)
            continue;
        /* Meshes are authored nose-toward +Z and model rotation takes the
         * opposite sign from the view, so heading yaw is 180 - yaw. */
        a2_scene_draw_shadow(scene, c->model, c->x, c->z, c->chunk, 180.0f - c->yaw);
        a2_scene_draw_model(scene, c->model, c->x, c->y, c->z, 180.0f - c->yaw);
    }
}

int a2_traffic_collide(a2_traffic *t, float *x, float *z, float radius, float push[2], int *who)
{
    if (!t)
        return 0;
    int hit = 0;
    for (int i = 0; i < t->count; i++) {
        traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;
        /* `radius` is the player's own half-width; the separation that matters
         * is that plus the other vehicle's, measured from its body axis. */
        float sep = radius + c->half_wid;
        float bx, bz;
        body_axis_nearest(c, *x, *z, &bx, &bz);
        float dx = *x - bx, dz = *z - bz;
        float d2 = dx * dx + dz * dz;
        if (d2 >= sep * sep)
            continue;
        float d = sqrtf(d2);
        if (d < 1e-4f) {
            dx = 1.0f;
            dz = 0.0f;
            d = 1.0f;
        }
        *x = bx + dx / d * sep;
        *z = bz + dz / d * sep;
        if (push) {
            push[0] = dx / d;
            push[1] = dz / d;
        }
        if (who)
            *who = i;
        hit = 1;
    }
    return hit;
}

int a2_traffic_count(const a2_traffic *t) { return t ? t->count : 0; }

/* the yaw convention of place(): forward = (sin yaw, -cos yaw) */
int a2_traffic_car_motion(const a2_traffic *t, int i, float *vx, float *vz, const char **anim)
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return 0;
    const traffic_car *c = &t->cars[i];
    float r = c->yaw * (float)M_PI / 180.0f;
    *vx = sinf(r) * c->speed;
    *vz = -cosf(r) * c->speed;
    if (anim)
        *anim = c->anim;
    return 1;
}

int a2_traffic_car_box(const a2_traffic *t, int i, struct a2_cbox *box)
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return 0;
    const traffic_car *c = &t->cars[i];
    box->x0 = -c->half_wid;
    box->x1 = c->half_wid;
    box->z0 = -c->half_len;
    box->z1 = c->half_len;
    box->x = c->x;
    box->z = c->z;
    /* forward is (sin yaw, -cos yaw); the engine's is (-sin h, cos h) */
    box->heading = (c->yaw + 180.0f) * (4096.0f / 360.0f);
    box->fixed = 0;
    return 1;
}

void a2_traffic_car_push(a2_traffic *t, int i, float dvx, float dvz)
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return;
    traffic_car *c = &t->cars[i];
    if (t->driven) {
        /* an engine object: the impulse goes into its velocity, and its
         * own AI takes the lateral part out again next frame */
        a2_ai_push(&c->d, dvx, dvz, 0.0f);
        c->speed = c->d.v_fwd;
        return;
    }
    float r = c->yaw * (float)M_PI / 180.0f;
    c->speed += sinf(r) * dvx - cosf(r) * dvz;
    if (c->speed < 0.0f)
        c->speed = 0.0f;
}

int a2_traffic_car_pos(const a2_traffic *t, int i, float p[3], int *chunk)
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return 0;
    const traffic_car *c = &t->cars[i];
    p[0] = c->x;
    p[1] = c->y;
    p[2] = c->z;
    if (chunk)
        *chunk = c->chunk;
    return 1;
}

a2_drive *a2_traffic_drive(a2_traffic *t, int i, float *hl, float *hw, const char **anim)
{
    if (!t || !t->driven || i < 0 || i >= t->count || !t->cars[i].alive)
        return NULL;
    traffic_car *c = &t->cars[i];
    if (hl)
        *hl = c->half_len;
    if (hw)
        *hw = c->half_wid;
    if (anim)
        *anim = c->anim;
    return &c->d;
}

void a2_traffic_sync(a2_traffic *t, const a2_scene *scene, const struct a2_aiw *aiw)
{
    for (int i = 0; t && t->driven && aiw && i < t->count; i++)
        if (t->cars[i].alive)
            sync_car(t, &t->cars[i], scene, aiw);
}

int a2_traffic_occupied(const a2_traffic *t, float x, float z, float radius)
{
    if (!t)
        return 0;
    for (int i = 0; i < t->count; i++) {
        const traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;
        /* Against the body axis, so a long vehicle blocks its whole length --
         * otherwise a bus straddling two probe points reads as no obstacle. */
        float bx, bz;
        body_axis_nearest(c, x, z, &bx, &bz);
        float dx = x - bx, dz = z - bz;
        float sep = radius + c->half_wid;
        if (dx * dx + dz * dz < sep * sep)
            return 1;
    }
    return 0;
}
int a2_traffic_car_xz(const a2_traffic *t, int i, float out[2])
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return 0;
    out[0] = t->cars[i].x;
    out[1] = t->cars[i].z;
    return 1;
}


float a2_traffic_car_speed(const a2_traffic *t, int i)
{
    return (t && i >= 0 && i < t->count && t->cars[i].alive) ? t->cars[i].speed : 0.0f;
}

int a2_traffic_lane_changes(const a2_traffic *t)
{
    return t ? t->lane_changes : 0;
}

int a2_traffic_sound_sources(const a2_traffic *t, a2_sound_src *out, int max)
{
    int n = 0;
    for (int i = 0; t && i < t->count && n < max; i++) {
        const traffic_car *c = &t->cars[i];
        if (!c->alive)
            continue;
        out[n].key = 2000 + i;
        out[n].x = c->x;
        out[n].z = c->z;
        out[n].anim = c->anim;
        out[n].sample = -1;
        n++;
    }
    return n;
}
