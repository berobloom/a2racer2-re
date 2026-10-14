#include "paths.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SLOTS 10 /* 0x4e0580 .. 0x4e0710, 40 bytes each */

enum { T_BOAT = 3, T_TRAIN = 4, T_HELI = 5, T_TRAFFIC = 6, T_PLANE = 7 };

/* The five scripted-traffic meshes, the table at 0x4969F8, in its order. */
static const char *const TRAFFIC_ANIM[5] = {"N_BUS_ANIM", "N_VRTAUT_ANIM", "N_BESTEL_ANIM",
                                            "N_GOLF_ANIM", "N_ASTRA_ANIM"};

/* Tail rotor attachment, set at the top of the helicopter's start (0x433198). */
static const float TAIL_ROTOR[3] = {0.026f, 7.06f, -77.58f};

typedef struct {
    int active;
    int path;
    int type;
    float elapsed, duration; /* seconds */
    int model;
    float spin; /* helicopter blades, degrees */
    float x, y, z, yaw, pitch;
} path_obj;

struct a2_paths {
    const a2_track *track;
    path_obj obj[SLOTS];
    int *ran; /* per path: has run this lap (record +0x338) */
    int last_laps;
    int m_boat, m_train, m_plane, m_heli, m_lblade, m_sblade;
    int m_traffic[5];
    unsigned rng;
};

static unsigned prand(a2_paths *p)
{
    p->rng = p->rng * 1103515245u + 12345u;
    return p->rng >> 16;
}

a2_paths *a2_paths_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                          const a2_track *track)
{
    if (!track || !track->path_count)
        return NULL;
    a2_paths *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->ran = calloc(track->path_count, sizeof *p->ran);
    p->track = track;
    p->rng = 0x51f15eedu;
    p->m_boat = p->m_train = p->m_plane = p->m_heli = p->m_lblade = p->m_sblade = -1;
    for (int i = 0; i < 5; i++)
        p->m_traffic[i] = -1;
    int used[8] = {0};
    for (uint32_t i = 0; i < track->path_count; i++)
        if (track->paths[i].f2 < 8)
            used[track->paths[i].f2] = 1;
    if (used[T_BOAT])
        p->m_boat = a2_scene_load_model(scene, rcs, anims, "E00_ROND_ANIM");
    if (used[T_TRAIN])
        p->m_train = a2_scene_load_model(scene, rcs, anims, "C_TREIN_ANIM");
    if (used[T_PLANE])
        p->m_plane = a2_scene_load_model(scene, rcs, anims, "A_VLIEGT_ANIM");
    if (used[T_HELI]) {
        p->m_heli = a2_scene_load_model(scene, rcs, anims, "C_HELI_ANIM");
        p->m_lblade = a2_scene_load_model(scene, rcs, anims, "C_LBLADE_ANIM");
        p->m_sblade = a2_scene_load_model(scene, rcs, anims, "C_SBLADE_ANIM");
    }
    if (used[T_TRAFFIC])
        for (int i = 0; i < 5; i++)
            p->m_traffic[i] = a2_scene_load_model(scene, rcs, anims, TRAFFIC_ANIM[i]);
    fprintf(stderr, "paths: %u paths, types%s%s%s%s%s\n", track->path_count,
            used[T_BOAT] ? " boat" : "", used[T_TRAIN] ? " train" : "",
            used[T_HELI] ? " helicopter" : "", used[T_TRAFFIC] ? " traffic" : "",
            used[T_PLANE] ? " plane" : "");
    return p;
}

void a2_paths_free(a2_paths *p)
{
    if (!p)
        return;
    free(p->ran);
    free(p);
}

int a2_paths_active(const a2_paths *p)
{
    int n = 0;
    for (int i = 0; p && i < SLOTS; i++)
        n += p->obj[i].active;
    return n;
}

/* 0x433b10 for an open path: segment i and fraction t from the share of the
 * time elapsed, points i-1..i+2 (clamped at the ends), tangents
 * (p[i+1] - p[i-1]) * 0.5 and (p[i+2] - p[i]) * 0.5, Hermite blend. */
static void curve(const a2_path *ph, float u, float out[3])
{
    int n = (int)ph->point_count;
    if (n <= 1) {
        out[0] = n ? ph->points[0].x : 0.0f;
        out[1] = n ? ph->points[0].y : 0.0f;
        out[2] = n ? ph->points[0].z : 0.0f;
        return;
    }
    int segs = n - 1;
    float s = u * (float)segs;
    if (s < 0.0f)
        s = 0.0f;
    int i = (int)s;
    if (i > segs - 1)
        i = segs - 1;
    float t = s - (float)i;
    if (t > 1.0f)
        t = 1.0f;
    int i0 = i > 0 ? i - 1 : 0, i2 = i + 1, i3 = i + 2 < n ? i + 2 : n - 1;
    const a2_path_point *P0 = &ph->points[i0], *P1 = &ph->points[i], *P2 = &ph->points[i2],
                        *P3 = &ph->points[i3];
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    const float k = 0.5f; /* the tension every start function passes */
    float a[3] = {P0->x, P0->y, P0->z}, b[3] = {P1->x, P1->y, P1->z}, c[3] = {P2->x, P2->y, P2->z},
          d[3] = {P3->x, P3->y, P3->z};
    for (int j = 0; j < 3; j++) {
        float m1 = (c[j] - a[j]) * k, m2 = (d[j] - b[j]) * k;
        out[j] = h00 * b[j] + h10 * m1 + h01 * c[j] + h11 * m2;
    }
}

static void start(a2_paths *p, int path)
{
    const a2_path *ph = &p->track->paths[path];
    int type = (int)ph->f2;
    int model = type == T_BOAT    ? p->m_boat
                : type == T_TRAIN ? p->m_train
                : type == T_PLANE ? p->m_plane
                : type == T_HELI  ? p->m_heli
                : type == T_TRAFFIC ? p->m_traffic[prand(p) % 5u]
                                    : -1;
    if (model < 0 || !ph->point_count || !ph->f4)
        return;
    for (int i = 0; i < SLOTS; i++) {
        path_obj *o = &p->obj[i];
        if (o->active)
            continue;
        memset(o, 0, sizeof *o);
        o->active = 1;
        o->path = path;
        o->type = type;
        o->model = model;
        o->duration = (float)ph->f4 / 1000.0f;
        float pt[3];
        curve(ph, 0.0f, pt);
        o->x = pt[0];
        o->y = pt[1];
        o->z = pt[2];
        fprintf(stderr, "paths: path %d (type %d) starts, %.1f s over %u points\n", path, type,
                o->duration, ph->point_count);
        return;
    }
    /* all ten slots busy: the original's 0x434220 returns -1 and nothing runs */
}

void a2_paths_on_chunk(a2_paths *p, int chunk, int laps_done)
{
    if (!p)
        return;
    const a2_track *tr = p->track;
    /* Releases first: a path's object goes when the player reaches its
     * last_chunk (0x4347d7). */
    for (int i = 0; i < SLOTS; i++) {
        path_obj *o = &p->obj[i];
        if (o->active && tr->paths[o->path].last_chunk != 0 &&
            (int)tr->paths[o->path].last_chunk == chunk)
            o->active = 0;
    }
    /* 0x434460: a new lap clears every path's "ran" mark. */
    if (laps_done != p->last_laps) {
        memset(p->ran, 0, sizeof *p->ran * tr->path_count);
        p->last_laps = laps_done;
    }
    for (uint32_t i = 0; i < tr->path_count; i++) {
        const a2_path *ph = &tr->paths[i];
        if (ph->first_chunk == 0 || (int)ph->first_chunk != chunk || p->ran[i])
            continue;
        if (ph->f3 != 0 && (int)ph->f3 != laps_done + 1)
            continue;
        p->ran[i] = 1;
        start(p, (int)i);
    }
}

void a2_paths_update(a2_paths *p, float dt)
{
    if (!p)
        return;
    for (int i = 0; i < SLOTS; i++) {
        path_obj *o = &p->obj[i];
        if (!o->active)
            continue;
        const a2_path *ph = &p->track->paths[o->path];
        if (o->type == T_HELI)
            o->spin += 180.0f; /* 512 of 1024 units per frame, 0x47f974 */
        if (o->elapsed >= o->duration) {
            /* Scripted traffic starts over with a new vehicle and a new
             * duration of rand() % 5 + 5 seconds (0x434e9d..0x434f1f); the
             * rest stay where they ended. */
            if (o->type == T_TRAFFIC) {
                o->model = p->m_traffic[prand(p) % 5u];
                o->duration = (float)(prand(p) % 5u + 5u);
                o->elapsed = 0.0f;
            } else {
                continue;
            }
        }
        o->elapsed += dt;
        float u = o->elapsed / o->duration, u2 = (o->elapsed + dt) / o->duration;
        if (u > 1.0f)
            u = 1.0f;
        if (u2 > 1.0f)
            u2 = 1.0f;
        float a[3], b[3];
        curve(ph, u, a);
        curve(ph, u2, b);
        float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        if (o->type == T_HELI)
            a[1] -= 5.0f; /* 0x47f970: the body sits 5 below the curve */
        o->x = a[0];
        o->y = a[1];
        o->z = a[2];
        float h = sqrtf(dx * dx + dz * dz);
        if (h > 1e-4f) {
            /* atan2(dx, dz), as the original's heading; the meshes are
             * authored nose-toward +Z. */
            o->yaw = atan2f(dx, dz) * 180.0f / (float)M_PI;
            /* The helicopter and the traffic car take a full orientation from
             * the 3D direction, so they pitch with the path. */
            if (o->type == T_HELI || o->type == T_TRAFFIC)
                o->pitch = -atan2f(dy, h) * 180.0f / (float)M_PI;
        }
    }
}

void a2_paths_draw(const a2_paths *p, const a2_scene *scene)
{
    if (!p)
        return;
    for (int i = 0; i < SLOTS; i++) {
        const path_obj *o = &p->obj[i];
        if (!o->active)
            continue;
        if (o->type != T_HELI) {
            a2_scene_draw_model_rpy(scene, o->model, o->x, o->y, o->z, o->yaw, o->pitch, 0.0f);
            continue;
        }
        glPushMatrix();
        glTranslatef(o->x, o->y, o->z);
        glRotatef(o->yaw, 0.0f, 1.0f, 0.0f);
        glRotatef(o->pitch, 1.0f, 0.0f, 0.0f);
        a2_scene_draw_model_rpy(scene, o->model, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        a2_scene_draw_model_rpy(scene, p->m_lblade, 0.0f, 0.0f, 0.0f, o->spin, 0.0f, 0.0f);
        a2_scene_draw_model_rpy(scene, p->m_sblade, TAIL_ROTOR[0], TAIL_ROTOR[1], TAIL_ROTOR[2],
                                0.0f, o->spin, 0.0f);
        glPopMatrix();
    }
}

int a2_paths_sound_sources(const a2_paths *p, a2_sound_src *out, int max)
{
    int n = 0;
    for (int i = 0; p && i < SLOTS && n < max; i++) {
        const path_obj *o = &p->obj[i];
        if (!o->active)
            continue;
        /* 0x434b01 etc.: 45 trein, 46 heli, 32 auto2, 44 vliegt; the boat
         * sets no sample. */
        int s = o->type == T_TRAIN     ? 45
                : o->type == T_HELI    ? 46
                : o->type == T_TRAFFIC ? 32
                : o->type == T_PLANE   ? 44
                                       : -1;
        if (s < 0)
            continue;
        out[n].key = 4000 + i;
        out[n].x = o->x;
        out[n].z = o->z;
        out[n].sample = s;
        out[n].anim = NULL;
        n++;
    }
    return n;
}

int a2_paths_first(const a2_paths *p, float pos[3], float *yaw)
{
    for (int i = 0; p && i < SLOTS; i++)
        if (p->obj[i].active) {
            pos[0] = p->obj[i].x;
            pos[1] = p->obj[i].y;
            pos[2] = p->obj[i].z;
            *yaw = p->obj[i].yaw;
            return 1;
        }
    return 0;
}
