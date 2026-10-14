#include "traffic.h"

#include "ai_lane.h"
#include "contact.h"

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
} TRAFFIC_BEH[2] = {
    {11.5f, 2.55f, 25.0f, 13.0f, 200.0f, 200.0f, 300.0f}, /* behaviour 2 */
    {21.0f, 3.90f, 25.0f, 14.5f, 200.0f, 200.0f, 300.0f}, /* behaviour 3 */
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
    float t;        /* 0..1 along the segment from `chunk` to `chunk`+1 */
    float x, y, z, yaw;
    int alive;
} traffic_car;

struct a2_traffic {
    traffic_car *cars;
    int count;
    int models[TRAFFIC_KIND_COUNT];
    const char *anims[TRAFFIC_KIND_COUNT];
    int kind_slot[TRAFFIC_KIND_COUNT]; /* TRAFFIC_KINDS index -> loaded slot, -1 */
    float *limit; /* per-chunk AI speed limit, m/s; NULL = none */
    a2_traffic_other others[16];
    int other_count;
    int limit_count;
    float half_lens[TRAFFIC_KIND_COUNT];
    float half_wids[TRAFFIC_KIND_COUNT];
    int model_count;
    unsigned seed;
    int lane_changes; /* diagnostic: how often traffic has pulled over */
};

static unsigned next_rand(unsigned *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) & 0x7FFF;
}

/* Place a car at (chunk, lane, t) and work out its facing from the segment. */
/* Position on one lane's segment at `c->t`. */
static int lane_point_at(const traffic_car *c, const a2_scene *scene, int lane, float out[3])
{
    float a[3], b[3];
    if (!a2_scene_lane_point(scene, c->chunk, lane, a))
        return 0;
    int n = a2_scene_lane_count(scene, c->chunk + 1);
    if (n <= 0)
        return 0;
    if (!a2_scene_lane_point(scene, c->chunk + 1, lane < n ? lane : n - 1, b))
        return 0;
    out[0] = a[0] + (b[0] - a[0]) * c->t;
    out[1] = a[1] + (b[1] - a[1]) * c->t;
    out[2] = a[2] + (b[2] - a[2]) * c->t;
    return 1;
}

static int place(traffic_car *c, const a2_scene *scene)
{
    float a[3], b[3];
    if (!a2_scene_lane_point(scene, c->chunk, c->lane, a))
        return 0;
    int n = a2_scene_lane_count(scene, c->chunk + 1);
    if (n <= 0)
        return 0;
    int lane_b = c->lane < n ? c->lane : n - 1;
    if (!a2_scene_lane_point(scene, c->chunk + 1, lane_b, b))
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
        out[n++] = (seen){c->x, c->y, c->z, c->speed, c->chunk, c->lane, c->t};
    }
    for (int i = 0; i < t->other_count && n < max; i++) {
        const a2_traffic_other *o = &t->others[i];
        int ch = a2_scene_nearest_chunk(scene, o->x, o->z);
        int ln = a2_scene_nearest_lane(scene, ch, o->x, o->z);
        if (ch < 0 || ln < 0)
            continue;
        out[n++] = (seen){o->x, o->y, o->z, o->speed, ch, ln, 0.5f};
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

static void spawn(a2_traffic *t, traffic_car *c, const a2_scene *scene, int chunk)
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
    int lanes = a2_scene_lane_count(scene, chunk);
    c->lane = lanes > 0 ? (int)(next_rand(&t->seed) % (unsigned)lanes) : 0;
    c->t = (float)(next_rand(&t->seed) % 1000) / 1000.0f;
    c->lane_from = c->lane;
    c->lane_mix = 1.0f;
    c->recheck = (float)(next_rand(&t->seed) % 300) / 100.0f;
    c->alive = place(c, scene);
}

a2_traffic *a2_traffic_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                              int count, int start_chunk)
{
    a2_traffic *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->seed = 12345u;

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

    t->cars = calloc(count ? count : 1, sizeof(traffic_car));
    if (!t->cars) {
        free(t);
        return NULL;
    }
    t->count = count;
    /* Spread them down the road ahead of the player. */
    for (int i = 0; i < count; i++)
        spawn(t, &t->cars[i], scene, start_chunk + 3 + i * 4);
    int alive = 0;
    for (int i = 0; i < count; i++)
        if (t->cars[i].alive)
            alive++;
    fprintf(stderr, "traffic: %d cars (%d placed), %d models\n", count, alive, t->model_count);
    return t;
}

void a2_traffic_free(a2_traffic *t)
{
    if (!t)
        return;
    free(t->cars);
    free(t->limit);
    free(t);
}

void a2_traffic_set_limits(a2_traffic *t, const a2_track *track)
{
    /* The AI's own reading of the speed triggers (0x407ba0), which is not the
     * police's or the speed camera's: 4 -> 60 km/h, 5 -> 80, 6 -> 120, 8 -> no
     * limit, 10 -> stop (not used by the shipped tracks), 7 and 9 nothing.  A
     * car takes the limit as it enters the chunk and keeps it until the next
     * trigger, so this is the limit in force on each chunk for a car that has
     * driven there from the start. */
    if (!t || !track)
        return;
    int n = (int)track->pmp.chunk_count;
    free(t->limit);
    t->limit = calloc(n ? (size_t)n : 1, sizeof(float));
    if (!t->limit)
        return;
    t->limit_count = n;
    float cur = 500.0f;
    for (int i = 0; i < n; i++) {
        uint32_t tr[2] = {track->pmp.chunks[i].trigger0, track->pmp.chunks[i].trigger1};
        for (int k = 0; k < 2; k++) {
            switch (tr[k]) {
            case 4: cur = 16.67f; break;
            case 5: cur = 22.22f; break;
            case 6: cur = 33.33f; break;
            case 8: cur = 500.0f; break;
            default: break;
            }
        }
        t->limit[i] = cur;
    }
}

void a2_traffic_update(a2_traffic *t, const a2_scene *scene, float dt, int player_chunk)
{
    if (!t)
        return;
    const float UNITS_PER_METRE = 10.0f;
    static seen vis[256];
    int nvis = gather(t, scene, vis, 256);
    for (int i = 0; i < t->count; i++) {
        traffic_car *c = &t->cars[i];
        if (!c->alive) {
            spawn(t, c, scene, player_chunk + 20 + (i % 10) * 3);
            continue;
        }

        /* Advance along the current segment, measuring it in world units so a
         * speed in m/s means the same thing everywhere. */
        float a[3], b[3];
        if (!a2_scene_lane_point(scene, c->chunk, c->lane, a)) {
            c->alive = 0;
            continue;
        }
        int n = a2_scene_lane_count(scene, c->chunk + 1);
        int lane_b = (n > 0 && c->lane < n) ? c->lane : (n > 0 ? n - 1 : 0);
        if (n <= 0 || !a2_scene_lane_point(scene, c->chunk + 1, lane_b, b)) {
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
            if (t->limit && c->chunk >= 0 && c->chunk < t->limit_count)
                allowed = t->limit[c->chunk];
            float sin_a = 0.0f;
            int n2 = a2_scene_lane_count(scene, c->chunk + 2);
            float p2[3];
            if (n2 > 0 && a2_scene_lane_point(scene, c->chunk + 2, c->lane < n2 ? c->lane : n2 - 1,
                                              p2)) {
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
            int lanes = a2_scene_lane_count(scene, c->chunk);
            if (lanes > 0 && c->lane < lanes) {
                lane_info L[8];
                if (lanes > 8)
                    lanes = 8;
                seen self = {c->x, c->y, c->z, c->speed, c->chunk, c->lane, c->t};
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
            c->chunk++;
            int ln = a2_scene_lane_count(scene, c->chunk);
            if (ln <= 0) {
                c->alive = 0;
                break;
            }
            if (c->lane >= ln)
                c->lane = ln - 1;
        }
        if (!c->alive)
            continue;

        /* Recycle anything the player has left well behind, or that ran off
         * the end of the stage. */
        if (c->chunk < player_chunk - 10 || !place(c, scene))
            c->alive = 0;
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
    return 1;
}

void a2_traffic_car_push(a2_traffic *t, int i, float dvx, float dvz)
{
    if (!t || i < 0 || i >= t->count || !t->cars[i].alive)
        return;
    traffic_car *c = &t->cars[i];
    float r = c->yaw * (float)M_PI / 180.0f;
    c->speed += sinf(r) * dvx - cosf(r) * dvz;
    if (c->speed < 0.0f)
        c->speed = 0.0f;
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
