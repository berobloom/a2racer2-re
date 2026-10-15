/* The police on the new AI model; see cops.h. */
#include "cops.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COPS 32
#define RECORDS 12 /* records 3..14 */

enum {
    CS_CHASE = 3,   /* drive the road after the target */
    CS_AHEAD = 4,   /* the target is behind: block it */
    CS_CAUGHT = 5,
    CS_STAND = 6,
    CS_DONE = 8,    /* coast, then retire */
    CS_RETIRE = 9,  /* engine off at the finish */
    CS_UNSTICK = 10,
    CS_TURNED = 14, /* the target faces the wrong way */
    CS_GAVE_UP = 15,
    CS_GHOST = 16,  /* the target drives against its lane */
    CS_LEAVE = 17,  /* drive off at 10 m/s */
};

/* A chase target: the player, or racer k. */
enum { T_NONE = -2, T_PLAYER = -1 };

typedef struct {
    a2_ai_car c;
    int chunk, wp;
    int rec;     /* its record, -1 when parked */
    int retired; /* freed by state 8 beyond 30 chunks (object +0x34 = 0) */
    int siren;   /* 0x40c640 */
} cop_car;

typedef struct {
    int cop; /* -1 = free */
    int state;
    int target; /* T_PLAYER or a racer index */
    float timer, chunk_time, rev_time;
    int twp;
    float near[3], far[3];
    int logged; /* the state last logged (A2_AI_LOG) */
} record;

struct a2_cops {
    cop_car cops[MAX_COPS];
    int count;
    record rec[RECORDS];
    const a2_aiw *w;
    const a2_scene *scene;
    a2_ai_env env;
    a2_cops_config cfg;
    int model;
    int times_caught; /* g_times_caught */
    int message;      /* language.ini id, 0 for none */
    int message_next; /* the arrest shows two in turn, as the original does */
    float message_at;
    float clock;
    unsigned rng;
    int voice; /* 1 a chase began, 2 one was given up: the radio lines */
};

static int rnd(a2_cops *c)
{
    c->rng = c->rng * 214013u + 2531011u;
    return (int)((c->rng >> 16) & 0x7fff);
}

static int height_fn(void *ud, float x, float z, float *y)
{
    const a2_cops *c = ud;
    int ch = a2_scene_nearest_chunk(c->scene, x, z);
    return a2_scene_road_height(c->scene, ch, x, z, y);
}

a2_cops *a2_cops_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                        const a2_track *track, const a2_aiw *w, const a2_cops_config *cfg)
{
    if (!scene || !track || !w || !cfg || !cfg->player_car)
        return NULL;
    a2_cops *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->w = w;
    c->scene = scene;
    c->cfg = *cfg;
    c->rng = 7;
    c->env.car = cfg->player_car;
    c->env.gearbox = cfg->gearbox;
    c->env.tyres = cfg->tyres;
    c->env.demo = cfg->demo;
    c->env.height = height_fn;
    c->env.height_ud = c;
    c->model = a2_scene_load_model(scene, rcs, anims, "MPOLIZEI_ANIM");
    if (c->model < 0)
        fprintf(stderr, "cops: MPOLIZEI_ANIM not loaded; cops will not be drawn\n");
    for (uint32_t i = 0; i < track->object_count && c->count < MAX_COPS; i++) {
        const a2_object *o = &track->objects[i];
        if (o->type != 9)
            continue;
        cop_car *k = &c->cops[c->count++];
        memset(k, 0, sizeof *k);
        /* drawn as the placement is: the model yaw is -heading in degrees */
        a2_drive_init(&k->c.d, o->x, o->y, o->z, -o->rot_y * (4096.0f / 360.0f));
        while (k->c.d.heading < 0.0f)
            k->c.d.heading += 4096.0f;
        k->chunk = o->chunk;
        k->wp = a2_aiw_nearest(w, o->chunk, o->x, o->y, o->z, 1);
        k->rec = -1;
    }
    for (int r = 0; r < RECORDS; r++)
        c->rec[r].cop = -1;
    a2_scene_hide_type(scene, 9); /* drawn here from now on */
    fprintf(stderr, "cops: %d placed (new AI model)\n", c->count);
    return c;
}

void a2_cops_free(a2_cops *c) { free(c); }

int a2_cops_times_caught(const a2_cops *c) { return c ? c->times_caught : 0; }

int a2_cops_in_pursuit(const a2_cops *c)
{
    for (int i = 0; c && i < c->count; i++)
        if (c->cops[i].siren)
            return 1;
    return 0;
}

int a2_cops_count(const a2_cops *c) { return c ? c->count : 0; }

int a2_cops_take_voice(a2_cops *c)
{
    if (!c)
        return 0;
    int v = c->voice;
    c->voice = 0;
    return v;
}

int a2_cops_message(const a2_cops *c, float *age)
{
    if (!c || !c->message)
        return 0;
    if (age)
        *age = c->clock - c->message_at;
    return c->message;
}

int a2_cops_car_xz(const a2_cops *c, int i, float out[2])
{
    if (!c || i < 0 || i >= c->count || c->cops[i].retired)
        return 0;
    out[0] = c->cops[i].c.d.x;
    out[1] = c->cops[i].c.d.z;
    return 1;
}

int a2_cops_siren(const a2_cops *c, int i)
{
    return c && i >= 0 && i < c->count && !c->cops[i].retired && c->cops[i].siren;
}

void a2_cops_register(const a2_cops *c, a2_aiw *w)
{
    for (int i = 0; c && i < c->count; i++) {
        const cop_car *k = &c->cops[i];
        if (!k->retired)
            a2_aiw_occ_add(w, k->wp, A2_AI_ID_COP + i, k->c.d.x, k->c.d.z, k->c.d.v_fwd);
    }
}

/* ------------------------------------------------------------- targets */

typedef struct {
    a2_ai_target t;
    float x, z;
    int ok;
} target_view;

static target_view view(const a2_ai_player *p, const a2_racers *racers, int who)
{
    target_view v;
    memset(&v, 0, sizeof v);
    if (who == T_PLAYER) {
        if (!p || !p->d)
            return v;
        v.t.d = p->d;
        v.t.chunk = p->chunk;
        v.t.wp = p->wp;
        v.t.state = -1;
        v.ok = 1;
    } else if (who >= 0) {
        v.ok = a2_racer_ai(racers, who, &v.t);
    }
    if (v.ok) {
        v.x = v.t.d->x;
        v.z = v.t.d->z;
    }
    return v;
}

/* 0x410a10: the target faces the way the road runs */
static int faces_road(const a2_cops *c, const target_view *v)
{
    float dx, dz;
    if (!a2_aiw_road_dir(c->w, v->t.chunk, 1, &dx, &dz))
        return 1;
    float h = v->t.d->heading * (6.28318548f / 4096.0f);
    /* forward is (-sin, cos) */
    return (-sinf(h) * dx + cosf(h) * dz) >= 0.0f;
}

static void siren(cop_car *k, int on) { k->siren = on; }

/* 0x4034a0: open a record for `cop` on `who` */
static void start_chase(a2_cops *c, int cop, int who, float dt)
{
    int max = who == T_PLAYER ? (c->cfg.difficulty ? 3 : 1) : 2;
    if (c->cfg.circuit && (float)(rnd(c) % 1000) > dt * 1000.0f)
        return;
    int free_r = -1, on_it = 0;
    for (int r = 0; r < RECORDS; r++) {
        if (c->rec[r].cop < 0 && free_r < 0)
            free_r = r;
        if (c->rec[r].cop >= 0 && c->rec[r].target == who)
            on_it++;
    }
    if (free_r < 0 || on_it >= max)
        return;
    record *R = &c->rec[free_r];
    memset(R, 0, sizeof *R);
    R->cop = cop;
    R->state = CS_CHASE;
    R->target = who;
    R->twp = -1;
    cop_car *k = &c->cops[cop];
    k->rec = free_r;
    k->c.throttle = k->c.brake = k->c.steer = 0.0f;
    k->c.engine_off = 0;
    siren(k, 1);
    /* 0x4150f0: the radio line, always for the player, one time in three
     * for a racer */
    if (who == T_PLAYER || rnd(c) % 3 == 0)
        c->voice = 1;
    fprintf(stderr, "cop %d: chasing %s from chunk %d\n", cop,
            who == T_PLAYER ? "the player" : "a racer", k->chunk);
}

/* 0x408080: the speed trigger in force at `chunk`, looking back */
static int backtrack_speed(const a2_aiw *w, int chunk)
{
    for (int k = chunk; k >= 0; k--)
        for (int s = 0; s < 2; s++) {
            int t = a2_aiw_trigger(w, k, s);
            if (t >= 4 && t <= 8)
                return t;
        }
    return 0;
}

/* 0x409060: a parked cop watches its own chunk */
static void watch(a2_cops *c, int i, const a2_ai_player *p, a2_racers *racers, float dt)
{
    cop_car *k = &c->cops[i];
    for (int j = 0; j < a2_racers_count(racers); j++) {
        a2_ai_target t;
        if (!a2_racer_ai(racers, j, &t) || t.chunk != k->chunk)
            continue;
        if (t.limit <= t.d->v_fwd) {
            start_chase(c, i, j, dt);
            return;
        }
    }
    if (!p || !p->d || p->chunk != k->chunk)
        return;
    static const float LIMIT[4] = {12.5f, 17.5f, 25.0f, 30.0f};
    int t = backtrack_speed(c->w, k->chunk);
    if (t >= 4 && t <= 7 && p->d->v_fwd > LIMIT[t - 4])
        start_chase(c, i, T_PLAYER, dt);
}

/* ------------------------------------------------- the transition table */

static float dist2(const cop_car *k, float x, float z)
{
    float dx = k->c.d.x - x, dz = k->c.d.z - z;
    return dx * dx + dz * dz;
}

/* 0x401960 */
static int t_behind(const cop_car *k, const target_view *v)
{
    int b = a2_ai_bearing(&k->c.d, v->x, v->z);
    return b > 1700 && b < 2300;
}

/* 0x4019f0 */
static int t_stopped(const cop_car *k, const target_view *v, const a2_ai_player *p)
{
    if (v->t.state == -1 && p->mode == 3)
        return 0;
    if (v->t.state == 9)
        return 0;
    if (fabsf(v->t.d->v_fwd) + fabsf(v->t.d->v_lat) >= 7.0f)
        return 0;
    return dist2(k, v->x, v->z) < 5000.0f;
}

static void transitions(a2_cops *c, record *R, const a2_ai_player *p, a2_racers *racers,
                        float dt)
{
    cop_car *k = &c->cops[R->cop];
    target_view v = view(p, racers, R->target);
    if (!v.ok)
        return;
    switch (R->state) {
    case CS_CHASE:
        if (t_behind(k, &v)) {
            R->state = CS_AHEAD;
            return;
        }
        if (t_stopped(k, &v, p)) {
            R->state = CS_CAUGHT;
            return;
        }
        if (a2_aiw_with_lane(c->w, v.t.wp, v.t.d) == -1 && faces_road(c, &v)) { /* 0x401f60 */
            R->state = CS_GHOST;
            return;
        }
        if (R->chunk_time > 3.0f) { /* 0x402050 */
            R->state = CS_UNSTICK;
            return;
        }
        { /* 0x401e20: give up */
            float lim = c->cfg.difficulty ? 60.0f : 30.0f;
            R->timer += dt;
            if (R->timer > lim) {
                R->timer = 0.0f;
                siren(k, 0);
                R->state = CS_GAVE_UP;
                if (R->target == T_PLAYER || rnd(c) % 3 == 0) /* 0x415170 */
                    c->voice = 2;
                fprintf(stderr, "cop %d: gave up after %.0f s\n", R->cop, lim);
                return;
            }
            if (a2_aiw_chunk_dist(c->w, k->chunk, v.t.chunk) > 50) {
                R->state = CS_GAVE_UP;
                if (R->target == T_PLAYER || rnd(c) % 3 == 0)
                    c->voice = 2;
                fprintf(stderr, "cop %d: gave up, 50 chunks apart\n", R->cop);
                return;
            }
        }
        if (v.t.d->v_fwd < 0.0f) /* 0x401fb0 */
            R->state = CS_TURNED;
        return;
    case CS_AHEAD:
        if (!t_behind(k, &v)) {
            R->state = CS_CHASE;
            return;
        }
        if (t_stopped(k, &v, p)) {
            R->state = CS_CAUGHT;
            return;
        }
        if (a2_aiw_with_lane(c->w, v.t.wp, v.t.d) == -1 && faces_road(c, &v)) {
            R->state = CS_GHOST;
            return;
        }
        if (!faces_road(c, &v)) { /* 0x401d70 */
            R->state = CS_TURNED;
            return;
        }
        if (v.t.d->v_fwd < 0.0f)
            R->state = CS_TURNED;
        return;
    case CS_CAUGHT:
        R->state = CS_STAND; /* 0x434140 */
        return;
    case CS_STAND:
        if (R->timer < 4.0f) { /* 0x401b00 */
            R->timer += dt;
            return;
        }
        R->timer = 0.0f;
        siren(k, 0);
        R->state = CS_LEAVE;
        return;
    case CS_LEAVE:
    case CS_GAVE_UP:
        if (k->c.d.v_fwd <= 5.0f) { /* 0x401ef0 */
            k->c.d.v_fwd = 0.0f;
            k->c.d.v_lat = 0.0f;
            k->c.d.yaw_rate = 0.0f;
            R->state = CS_DONE;
        }
        return;
    case CS_GHOST:
        if (faces_road(c, &v) && a2_aiw_with_lane(c->w, v.t.wp, v.t.d) == 1) { /* 0x402080 */
            R->state = CS_CHASE;
            return;
        }
        if (!faces_road(c, &v)) {
            R->state = CS_TURNED;
            return;
        }
        /* 0x401ff0: more than 2 s in the chunk while the target does > 10;
         * at 2 s or less it clears the timer, so it can never fire after
         * one frame -- kept */
        if (R->chunk_time > 2.0f) {
            if (v.t.d->v_fwd > 10.0f) {
                R->state = CS_UNSTICK;
                return;
            }
        } else {
            R->chunk_time = 0.0f;
        }
        if (v.t.d->v_fwd < 0.0f)
            R->state = CS_TURNED;
        return;
    case CS_TURNED:
        if (faces_road(c, &v) && a2_aiw_chunk_dist(c->w, v.t.chunk, k->chunk) < 2 &&
            v.t.d->v_fwd > 0.0f) /* 0x401d90 */
            R->state = CS_CHASE;
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------- the states */

static const int ORDER[3] = {A2_AIW_STRAIGHT, A2_AIW_LANE_DOWN, A2_AIW_LANE_UP};

static void own_wp(a2_cops *c, cop_car *k)
{
    int ch = a2_scene_nearest_chunk(c->scene, k->c.d.x, k->c.d.z);
    if (ch >= 0)
        k->chunk = ch;
    int wp = a2_aiw_nearest(c->w, k->chunk, k->c.d.x, k->c.d.y, k->c.d.z, 1);
    if (wp >= 0)
        k->wp = wp;
}

/* 0x409d10 for a cop (state 3, and 17's drive) */
static void st_drive(a2_cops *c, record *R, cop_car *k, float dt)
{
    k->c.engine_off = 0;
    int old = R->twp;
    int t = a2_aiw_scan_path(c->w, k->wp, ORDER, R->near, R->far);
    R->twp = t;
    if (t > -1) {
        if (!a2_aiw_blocked(c->w, k->wp, A2_AI_ID_COP + R->cop, &k->c.d))
            a2_ai_speed_control(&k->c, R->far, 1, dt);
    } else {
        R->twp = old;
    }
    a2_ai_drive(&k->c, &c->env, R->near, 1.0f, R->state == CS_CHASE ? 0.1f : 0.0f, 1, dt);
}

/* 0x409f60: in front of the target -- toward its lane, then block */
static void st_ahead(a2_cops *c, record *R, cop_car *k, const target_view *v, float dt)
{
    int order[3] = {A2_AIW_STRAIGHT, A2_AIW_LANE_DOWN, A2_AIW_LANE_UP};
    int tl = a2_aiw_lane_index(c->w, v->t.chunk, v->t.wp);
    int ol = a2_aiw_lane_index(c->w, k->chunk, k->wp);
    if (tl > ol) {
        order[0] = A2_AIW_LANE_UP;
        order[1] = A2_AIW_STRAIGHT;
        order[2] = A2_AIW_LANE_DOWN;
    } else if (tl < ol) {
        order[0] = A2_AIW_LANE_DOWN;
        order[1] = A2_AIW_STRAIGHT;
        order[2] = A2_AIW_LANE_UP;
    } else {
        order[0] = A2_AIW_STRAIGHT;
        order[1] = A2_AIW_LANE_UP;
        order[2] = A2_AIW_LANE_DOWN;
    }
    int old = R->twp;
    int t = a2_aiw_scan_path(c->w, k->wp, order, R->near, R->far);
    R->twp = t;
    if (t > -1) {
        if (v->t.d->v_fwd < k->c.d.v_fwd) {
            k->c.brake = 50.0f;
            k->c.throttle = 0.0f;
        } else {
            a2_ai_speed_control(&k->c, R->far, 1, dt);
            int b = a2_ai_bearing(&k->c.d, v->x, v->z);
            if (b > 1800 && b < 2200)
                k->c.brake = 100.0f;
        }
    } else {
        k->c.brake = 0.0f;
        k->c.throttle = 0.0f;
        R->twp = old;
    }
    a2_ai_drive(&k->c, &c->env, R->near, 1.0f, 0.0f, 1, dt);
}

static void stand(a2_cops *c, cop_car *k, float dt)
{
    k->c.d.v_fwd = 0.0f;
    k->c.d.v_lat = 0.0f;
    k->c.d.yaw_rate = 0.0f;
    a2_ai_body(&k->c, &c->env, dt);
}

/* 0x40a6a0 (16) and 0x40aab0 (14): follow, braking when ahead and faster */
static void st_follow(a2_cops *c, record *R, cop_car *k, const target_view *v, int ghost,
                      float dt)
{
    int old = R->twp;
    int t = a2_aiw_scan_path(c->w, k->wp, ORDER, R->near, R->far);
    R->twp = t;
    if (t > -1) {
        if (a2_aiw_ahead(c->w, k->chunk, v->t.chunk)) {
            if (k->c.d.v_fwd > v->t.d->v_fwd) {
                k->c.brake += 400.0f * dt;
                if (k->c.brake > 100.0f)
                    k->c.brake = 100.0f;
                k->c.throttle = 0.0f;
            }
        } else {
            a2_ai_speed_control(&k->c, R->far, 1, dt);
        }
        a2_ai_steer_to(&k->c, R->near);
    } else {
        R->twp = old;
    }
    a2_ai_longitudinal(&k->c, &c->env, 1.0f, 0.0f, dt);
    a2_ai_steering(&k->c, &c->env, 1, dt);
    if (k->c.d.v_fwd < 0.0f) {
        k->c.d.v_fwd = 0.0f;
        /* 0x40fba0 */
        k->c.d.v_lat -= k->c.d.v_lat * 10.0f * dt;
        k->c.d.v_fwd -= k->c.d.v_fwd * 10.0f * dt;
        k->c.d.yaw_rate -= k->c.d.yaw_rate * 10.0f * dt;
        k->c.brake = 0.0f;
    }
    if (!ghost || v->t.d->v_fwd <= 0.0f)
        R->chunk_time = 0.0f;
    a2_ai_body(&k->c, &c->env, dt);
}

/* 0x4035b0 */
static void free_record(a2_cops *c, record *R)
{
    if (R->cop >= 0) {
        c->cops[R->cop].rec = -1;
        siren(&c->cops[R->cop], 0);
    }
    memset(R, 0, sizeof *R);
    R->cop = -1;
}

static int handle(a2_cops *c, record *R, const a2_ai_player *p, a2_racers *racers, float dt)
{
    cop_car *k = &c->cops[R->cop];
    target_view v = view(p, racers, R->target);
    int caught = 0;
    own_wp(c, k);
    switch (R->state) {
    case CS_CHASE: st_drive(c, R, k, dt); break;
    case CS_AHEAD:
        if (v.ok)
            st_ahead(c, R, k, &v, dt);
        break;
    case CS_CAUGHT: /* 0x40a390 */
        R->timer = 0.0f;
        siren(k, 0);
        if (R->target == T_PLAYER) {
            if (p->mode == 0)
                caught = 1; /* set_game_mode(8) */
        } else {
            a2_racer_arrest(racers, R->target);
        }
        for (int r = 0; r < RECORDS; r++)
            if (c->rec[r].cop >= 0 && c->rec[r].target == R->target)
                c->rec[r].state = CS_STAND;
        a2_ai_body(&k->c, &c->env, dt);
        break;
    case CS_STAND: stand(c, k, dt); break;
    case CS_LEAVE: /* 0x40a8a0 */
        if (v.ok) {
            int b = a2_ai_bearing(&k->c.d, v.x, v.z);
            if (b > 0x400 && b < 0xc00) {
                st_drive(c, R, k, dt);
                k->c.d.v_fwd = 10.0f;
                R->timer += dt;
                if (R->timer > 3.0f)
                    k->c.d.v_fwd = 0.0f;
                break;
            }
        }
        a2_ai_body(&k->c, &c->env, dt);
        break;
    case CS_DONE: /* 0x40a4f0 */
        if (a2_aiw_chunk_dist(c->w, k->chunk, p->chunk) > 30) {
            k->retired = 1;
            free_record(c, R);
            return 0;
        }
        a2_ai_body(&k->c, &c->env, dt);
        k->c.d.v_fwd -= k->c.d.v_fwd * 3.0f * dt;
        k->c.d.v_lat -= k->c.d.v_lat * 3.0f * dt;
        k->c.d.yaw_rate -= k->c.d.yaw_rate * 3.0f * dt;
        break;
    case CS_RETIRE: /* 0x409ed0 */
        k->c.engine_off = 1;
        k->c.steer += 75.0f * dt;
        if (k->c.steer > 100.0f)
            k->c.steer = 100.0f;
        k->c.throttle = 0.0f;
        k->c.brake = 0.0f;
        a2_ai_steering(&k->c, &c->env, 1, dt);
        a2_ai_body(&k->c, &c->env, dt);
        break;
    case CS_UNSTICK: /* 0x40a190 */
        if (R->chunk_time >= 2.0f) {
            k->c.steer = 0.0f;
            k->c.d.v_fwd = -15.0f;
            R->chunk_time = 0.0f;
            R->rev_time = 0.0f;
            a2_ai_body(&k->c, &c->env, dt);
            break;
        }
        R->rev_time += dt;
        a2_ai_body(&k->c, &c->env, dt);
        if (R->rev_time > 1.0f) {
            R->state = CS_CHASE;
            R->rev_time = 0.0f;
        }
        break;
    case CS_TURNED:
        if (v.ok)
            st_follow(c, R, k, &v, 0, dt);
        break;
    case CS_GHOST:
        if (v.ok)
            st_follow(c, R, k, &v, 1, dt);
        break;
    case CS_GAVE_UP: { /* 0x40a9d0: engine off, coast down the road */
        k->c.engine_off = 1;
        int old = R->twp;
        int t = a2_aiw_scan_path(c->w, k->wp, ORDER, R->near, R->far);
        R->twp = t;
        if (t > -1) {
            if (!a2_aiw_blocked(c->w, k->wp, A2_AI_ID_COP + R->cop, &k->c.d))
                a2_ai_speed_control(&k->c, R->far, 1, dt);
        } else {
            R->twp = old;
        }
        a2_ai_drive(&k->c, &c->env, R->near, 1.0f, 0.0f, 1, dt);
        break;
    }
    default: break;
    }
    return caught;
}

/* 0x409540 for a cop: the time in the chunk */
static void track_chunk(a2_cops *c, record *R, cop_car *k, float dt)
{
    int ch = a2_scene_nearest_chunk(c->scene, k->c.d.x, k->c.d.z);
    if (ch < 0 || ch == k->chunk) {
        R->chunk_time += dt;
        return;
    }
    k->chunk = ch;
    R->chunk_time = 0.0f;
}

int a2_cops_update(a2_cops *c, const a2_ai_player *p, a2_racers *racers, float dt)
{
    if (!c || dt <= 0.0f)
        return 0;
    c->clock += dt;
    if (c->message && c->clock - c->message_at > 4.0f) {
        /* The arrest is a two-message sequence in the original: mode 6's
         * callbacks fetch 42 GEARRESTEERD and then 43 RACE OVER. */
        c->message = c->message_next;
        c->message_next = 0;
        c->message_at = c->clock;
    }
    c->env.player_skid = p ? p->skid : 0;
    c->env.player_stopped = p ? p->stopped : 0;

    /* HANDLE_COP_AI's ai_model-0 arm (0x4029d2), for every placed cop */
    for (int i = 0; i < c->count; i++) {
        cop_car *k = &c->cops[i];
        if (k->retired || k->rec >= 0)
            continue;
        watch(c, i, p, racers, dt);
        if (k->rec < 0) {
            /* 0x40fba0: parked, damped to a stop */
            k->c.d.v_lat -= k->c.d.v_lat * 10.0f * dt;
            k->c.d.v_fwd -= k->c.d.v_fwd * 10.0f * dt;
            k->c.d.yaw_rate -= k->c.d.yaw_rate * 10.0f * dt;
            a2_ai_integrate(&k->c, dt);
        }
    }
    /* 0x403340: the transitions; 0x408ed0: the states */
    for (int r = 0; r < RECORDS; r++)
        if (c->rec[r].cop >= 0)
            transitions(c, &c->rec[r], p, racers, dt);
    int caught = 0;
    static int log = -1;
    if (log < 0)
        log = getenv("A2_AI_LOG") != NULL;
    for (int r = 0; r < RECORDS; r++) {
        record *R = &c->rec[r];
        if (R->cop < 0)
            continue;
        cop_car *k = &c->cops[R->cop];
        if (log && R->state != R->logged) {
            fprintf(stderr, "cop %d (record %d): state %d, target %s, v %.1f, t=%.1f\n", R->cop,
                    r + 3, R->state, R->target == T_PLAYER ? "player" : "racer", k->c.d.v_fwd,
                    c->clock);
            R->logged = R->state;
        }
        if (handle(c, R, p, racers, dt))
            caught = 1;
        if (R->cop < 0)
            continue;
        a2_ai_integrate(&k->c, dt);
        track_chunk(c, R, k, dt);
        /* 0x408f7d: modes 3 and 6 put the sirens out; on a highway stage
         * the finish triggers retire the cop */
        if (p && (p->mode == 3 || p->mode == 6))
            siren(k, 0);
        if (!c->cfg.circuit) {
            int t0 = a2_aiw_trigger(c->w, k->chunk, 0), t1 = a2_aiw_trigger(c->w, k->chunk, 1);
            if (t0 == 14 || t0 == 15 || t1 == 14 || t1 == 15)
                R->state = CS_RETIRE;
        }
    }
    if (caught) {
        c->times_caught++;
        /* Three strikes: 49/50 EERSTE / LAATSTE WAARSCHUWING, then 42
         * GEARRESTEERD and 43 RACE OVER. */
        c->message = c->times_caught == 1 ? 49 : c->times_caught == 2 ? 50 : 42;
        c->message_next = c->times_caught >= 3 ? 43 : 0;
        c->message_at = c->clock;
    }
    return caught;
}

void a2_cops_draw(const a2_cops *c, const a2_scene *scene, int near_chunk, int radius)
{
    if (!c || c->model < 0)
        return;
    for (int i = 0; i < c->count; i++) {
        const cop_car *k = &c->cops[i];
        int d = a2_scene_chunk_delta(scene, near_chunk, k->chunk);
        if (k->retired || d < -radius || d > radius)
            continue;
        float yaw = -k->c.d.heading * (360.0f / 4096.0f);
        a2_scene_draw_shadow(scene, c->model, k->c.d.x, k->c.d.z, k->chunk, yaw);
        a2_scene_draw_model_rpy(scene, c->model, k->c.d.x, k->c.d.y, k->c.d.z, yaw,
                                k->c.d.pitch * (360.0f / 4096.0f), k->c.d.roll * (360.0f / 4096.0f));
    }
}

int a2_cops_sound_sources(const a2_cops *c, a2_sound_src *out, int max)
{
    int n = 0;
    for (int i = 0; c && i < c->count && n < max; i++) {
        const cop_car *k = &c->cops[i];
        if (k->rec < 0 || k->retired)
            continue; /* a parked cop is silent */
        out[n].key = 3000 + i;
        out[n].x = k->c.d.x;
        out[n].z = k->c.d.z;
        out[n].anim = "MPOLIZEI_ANIM";
        /* 0x40c640 swaps the object sample to SNDFX_POLITIE_SIRENE (40) while
         * the light is on, and back to its engine note */
        out[n].sample = k->siren ? 40 : -1;
        out[n].once = 0;
        n++;
    }
    return n;
}

/* The police car's size: half its mesh's XZ bounding box, which is the
 * original's collision shape for a registered object (0x449a30).  Every
 * police car on every stage is MPOLIZEI_ANIM, and mpolizei.d3d measures
 * x +-8.21, z +-19.39 (VPOLIZEI.d3d, 8.49 x 21.83, is placed nowhere). */
#define COP_HALF_WID 8.21f
#define COP_HALF_LEN 19.39f

void a2_cop_half_size(float *half_wid, float *half_len)
{
    *half_wid = COP_HALF_WID;
    *half_len = COP_HALF_LEN;
}

int a2_cops_collide(a2_cops *c, float *x, float *z, float radius, float push[2], int *who)
{
    int hit = 0;
    for (int i = 0; c && i < c->count; i++) {
        cop_car *k = &c->cops[i];
        if (!k->retired &&
            a2_ai_capsule_push(&k->c.d, COP_HALF_LEN, COP_HALF_WID, x, z, radius, push)) {
            if (who)
                *who = i;
            hit = 1;
        }
    }
    return hit;
}

a2_drive *a2_cop_drive(a2_cops *c, int i)
{
    return (c && i >= 0 && i < c->count && !c->cops[i].retired) ? &c->cops[i].c.d : NULL;
}
