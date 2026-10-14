/* The racers, on the shipped game's AI (Game.ini ai_model = 0): the records
 * 0-2 of the table at 0x4a55a0, run by the transition table at 0x4a5410 and
 * HandleAIObject (0x408a50).  See racers.h and research/discoveries.md, "The
 * new AI model, traced". */
#include "racers.h"
#include "wheels.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    ST_RACE = 0,    /* drive the road */
    ST_BEHIND = 1,  /* the player is close behind the leader */
    ST_BLOCK = 2,   /* cut in front of the player */
    ST_HELD = 7,    /* arrested: stand for 4 s */
    ST_DONE = 9,    /* finished: engine off, pull aside */
    ST_UNSTICK = 10,
    ST_WAIT = 11,   /* far ahead: 60 km/h */
};

typedef struct {
    int model;
    int car;          /* CARS.DAT slot of the mesh */
    a2_ai_car c;
    a2_wheels wheels;
    int alive;
    /* the AI record */
    int state;        /* +0x04 */
    int target;       /* +0x08: 1 = the player */
    int laps;         /* +0x0c */
    float timer;      /* +0x10 */
    float chunk_time; /* +0x14 */
    float rev_time;   /* +0x18 */
    int twp;          /* +0x1c */
    int chunk, prev;  /* +0x20, +0x24 */
    int fin;          /* +0x28 */
    float limit;      /* +0x2c, the speed the police judge it by */
    float far[3];     /* ScanPath's far point, kept for the fallback */
    float near[3];
    int wp;           /* object +0x20, its waypoint this frame */
    /* progress (0x4a5b40[ai].+0x60) and rank (0x485b28) */
    int progress;
    int progress0;
    int rank;
    int finished;
    float finish_time;
    int finish_pos;
} racer;

struct a2_racers {
    racer *cars;
    int count;
    const a2_aiw *w;
    const a2_scene *scene;
    a2_ai_env env;
    a2_racers_config cfg;
    a2_ai_player player;
    int wheel_model;  /* WIEL_ANIM */
    int finishers;
    unsigned rng;
};

/* The game's own (deliberately altered) car names, menu.dat's table at file
 * offset 0x2c958, in slot order.  They line up with the meshes RACER_1..7 name
 * (r_trabbi, r_astra, r_beetle, r_bmw, r_porche, r_clk, r_diablo) and with
 * garage.ini's item-section headers (`== BMW ITEMS ==` ...).  The comments in
 * that file's `[prices]` block disagree from car4 on; the table and the meshes
 * settle it. */
static const char *const CAR_NAME[A2_CAR_SLOTS] = {
    "Brabant",    "Opol Astro",       "Wolfswagen",  "BWM Z3.5", "PARCHE 913",
    "Mersedes SLK GTR", "Lambdaghini", "?", "?", "?"};

/* The MSVC rand(), for the 1 -> 2 chance. */
static int rnd(a2_racers *r)
{
    r->rng = r->rng * 214013u + 2531011u;
    return (int)((r->rng >> 16) & 0x7fff);
}

static int height_fn(void *ud, float x, float z, float *y)
{
    const a2_racers *r = ud;
    int ch = a2_scene_nearest_chunk(r->scene, x, z);
    return a2_scene_road_height(r->scene, ch, x, z, y);
}

static float dist2(const racer *c, float x, float z)
{
    float dx = c->c.d.x - x, dz = c->c.d.z - z;
    return dx * dx + dz * dz;
}

/* 0x4088f0: a racer's chunk triggers, every frame. */
static void on_trigger(a2_racers *r, racer *c, int t, float race_time)
{
    switch (t) {
    case 4: c->limit = 12.5f; break;
    case 5: c->limit = 17.5f; break;
    case 6: c->limit = 25.0f; break;
    case 8: c->limit = 30.0f; break;
    case 15: c->fin = 15; break;
    case 14:
        if (r->cfg.circuit) {
            if (c->fin != 15)
                return;
            c->laps++;
            c->fin = 14;
            if (c->laps < r->cfg.laps)
                return;
        }
        if (!c->finished) {
            c->finished = 1;
            c->finish_time = race_time;
            c->finish_pos = ++r->finishers;
            fprintf(stderr, "racer %s finished %d in %.1f s\n", CAR_NAME[c->car], c->finish_pos,
                    race_time);
        }
        c->state = ST_DONE;
        break;
    default: break;
    }
}

a2_racers *a2_racers_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                            const a2_cars *cars, const a2_aiw *w, const a2_racers_config *cfg)
{
    if (!w || !cfg || cfg->count <= 0 || !cfg->player_car)
        return NULL;
    a2_racers *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->cars = calloc((size_t)cfg->count, sizeof(racer));
    if (!r->cars) {
        free(r);
        return NULL;
    }
    r->w = w;
    r->scene = scene;
    r->wheel_model = a2_scene_load_model(scene, rcs, anims, "WIEL_ANIM");
    r->cfg = *cfg;
    r->rng = 1;
    /* The AI drives the player's car (0x40eb5b): CARS.DAT is indexed by the
     * player's car type, with the player's gearbox and tyre upgrades. */
    r->env.car = cfg->player_car;
    r->env.gearbox = cfg->gearbox;
    r->env.tyres = cfg->tyres;
    r->env.demo = cfg->demo;
    r->env.height = height_fn;
    r->env.height_ud = r;

    /* The original's selection (0x411200..0x4112a6): the player's car is
     * marked taken, then racer 0 takes the nearest free car at or above it
     * (0x411410), falling back to the nearest at or below (0x411460); racer
     * 1 the other way round; racer 2 like racer 0.  From the Trabant that is
     * Astra, Golf, BMW. */
    int used[7] = {0};
    int player_slot = cfg->player_slot;
    if (player_slot >= 0 && player_slot < 7)
        used[player_slot] = 1;
    int pool[7], pool_n = 0;
    for (int i = 0; i < cfg->count && i < 7; i++) {
        int pc = player_slot >= 0 && player_slot < 7 ? player_slot : 0;
        int pick = -1;
        for (int pass = 0; pass < 2 && pick < 0; pass++) {
            int up = ((i & 1) == 0) ? (pass == 0) : (pass == 1);
            if (up) {
                for (int k = pc; k < 7 && pick < 0; k++)
                    if (!used[k])
                        pick = k;
            } else {
                for (int k = pc; k >= 0 && pick < 0; k--)
                    if (!used[k])
                        pick = k;
            }
        }
        if (pick < 0)
            break;
        used[pick] = 1;
        if (!cars->cars[pick].empty)
            pool[pool_n++] = pick;
    }

    /* The grid (0x4111a9 .. 0x4112e5): the player's position is taken
     * first, each racer takes the next free one, 0..3, and stands at
     * STARTCHUNK - position (wrapped, 0x4426e0) in lane 4, which 0x442150
     * clamps to the chunk's last lane -- one car a chunk, in a line in the
     * player's lane. */
    int taken[4] = {0};
    if (cfg->player_position >= 0 && cfg->player_position < 4)
        taken[cfg->player_position] = 1;
    int n_chunks = a2_aiw_chunk_count(w);
    int placed = 0;
    for (int i = 0; i < cfg->count && i < pool_n && i < 3; i++) {
        racer *c = &r->cars[placed];
        int slot = pool[i];
        int m = a2_scene_load_model(scene, rcs, anims, a2_car_anim_name(slot));
        if (m < 0)
            continue;
        c->model = m;
        c->car = slot;
        a2_wheels_init(&c->wheels); /* 0x4136c0, called at 0x41139b */
        int gp = 0;
        while (gp < 4 && taken[gp])
            gp++;
        if (gp < 4)
            taken[gp] = 1;
        int chunk = cfg->start_chunk - gp;
        if (chunk < 0)
            chunk += n_chunks;
        int lanes = a2_scene_lane_count(scene, chunk);
        float pos[3], yaw;
        if (lanes <= 0 ||
            !a2_scene_start_pose_on_lane(scene, chunk, lanes - 1 < 4 ? lanes - 1 : 4, pos, &yaw))
            continue;
        a2_drive_init(&c->c.d, pos[0], pos[1], pos[2], (yaw - 180.0f) * (4096.0f / 360.0f));
        if (c->c.d.heading < 0.0f)
            c->c.d.heading += 4096.0f;
        /* 0x4093b0: state 0, no target waypoint, the chunk, progress 0 and
         * the speed limit backtracked from the start (0x408080 / 0x4088f0) */
        c->state = ST_RACE;
        c->target = -1;
        c->twp = -1;
        c->chunk = chunk;
        c->prev = -1;
        c->progress = chunk;
        c->progress0 = chunk;
        for (int k = chunk; k >= 0; k--) {
            int t0 = a2_aiw_trigger(w, k, 0), t1 = a2_aiw_trigger(w, k, 1);
            int t = (t0 >= 4 && t0 <= 8) ? t0 : (t1 >= 4 && t1 <= 8) ? t1 : 0;
            if (t) {
                on_trigger(r, c, t, 0.0f);
                break;
            }
        }
        c->wp = a2_aiw_nearest(w, chunk, pos[0], pos[1], pos[2], 1);
        memcpy(c->near, pos, sizeof pos);
        memcpy(c->far, pos, sizeof pos);
        c->alive = 1;
        placed++;
    }
    r->count = placed;
    if (!placed) {
        fprintf(stderr, "racers: none could be placed\n");
        free(r->cars);
        free(r);
        return NULL;
    }
    fprintf(stderr, "racers: %d opponents (new AI model, driving the player's car)", placed);
    for (int i = 0; i < placed; i++)
        fprintf(stderr, " %s", CAR_NAME[r->cars[i].car]);
    fprintf(stderr, "\n");
    return r;
}

void a2_racers_free(a2_racers *r)
{
    if (!r)
        return;
    free(r->cars);
    free(r);
}

void a2_racers_set_player(a2_racers *r, const a2_ai_player *p)
{
    if (r && p)
        r->player = *p;
}

void a2_racers_register(const a2_racers *r, a2_aiw *w)
{
    for (int i = 0; r && i < r->count; i++) {
        const racer *c = &r->cars[i];
        if (c->alive)
            a2_aiw_occ_add(w, c->wp, A2_AI_ID_RACER + i, c->c.d.x, c->c.d.z, c->c.d.v_fwd);
    }
}

/* ------------------------------------------------- the transition table */

/* 0x401780: the player is close behind this racer, and it leads */
static int c_player_behind(a2_racers *r, racer *c)
{
    if (c->timer > 1.0f) {
        c->timer = 0.0f;
        return 0;
    }
    const a2_ai_player *p = &r->player;
    if (dist2(c, p->x, p->z) >= 10000.0f)
        return 0;
    int b = a2_ai_bearing(&c->c.d, p->x, p->z);
    if (b <= 1500 || b >= 2500)
        return 0;
    c->target = 1;
    return c->rank == 0;
}

/* 0x4018b0 */
static int c_block_chance(a2_racers *r, racer *c, float dt)
{
    if (c->rank != 0 || c->c.d.v_fwd < 30.0f)
        return 0;
    if ((float)(rnd(r) % 100) < dt * 50.0f && c->timer < 1.0f)
        return 1;
    c->timer = 0.0f;
    return 0;
}

/* 0x401b60, for a racer: 2 when it is far ahead of the player */
static int c_far_ahead(a2_racers *r, racer *c)
{
    static const float LIM[4] = {2e7f, 2e7f, 1.75e7f, 1.5e7f};
    int rank = c->rank < 0 ? 0 : c->rank > 3 ? 3 : c->rank;
    if (dist2(c, r->player.x, r->player.z) <= LIM[rank])
        return 0;
    if (c->rank < r->player.rank)
        return 2;
    return r->cfg.difficulty ? 3 : 1;
}

static void transitions(a2_racers *r, racer *c, float dt)
{
    /* The table at 0x4a5410 in the order 0x409447 fills it; the first rule
     * whose state matches and whose condition holds wins. */
    switch (c->state) {
    case ST_RACE:
        if (!r->cfg.demo && c_far_ahead(r, c) == 2) {
            c->state = ST_WAIT;
            return;
        }
        if (c->chunk_time > 3.0f) { /* 0x402050 */
            c->state = ST_UNSTICK;
            return;
        }
        if (c_player_behind(r, c) == 1)
            c->state = ST_BEHIND;
        return;
    case ST_WAIT:
        if (!r->cfg.demo && dist2(c, r->player.x, r->player.z) < 1.5e7f) /* 0x401cf0 */
            c->state = ST_RACE;
        return;
    case ST_HELD:
        if (c->timer < 4.0f) { /* 0x401b00 */
            c->timer += dt;
            return;
        }
        c->timer = 0.0f;
        c->state = ST_RACE;
        return;
    case ST_BEHIND:
        if (c_block_chance(r, c, dt) == 1)
            c->state = ST_BLOCK;
        return;
    case ST_BLOCK:
        if (c_player_behind(r, c) == 0)
            c->state = ST_RACE;
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------- the states */

static const int ORDER[3] = {A2_AIW_STRAIGHT, A2_AIW_LANE_DOWN, A2_AIW_LANE_UP};

static float band(const a2_racers *r, int i, const racer *c)
{
    int lead = (c->progress - c->progress0) - (int)r->player.progress;
    return a2_ai_rubber_band(i, lead, r->cfg.difficulty, r->cfg.etappe);
}

static void own_wp(a2_racers *r, racer *c)
{
    int ch = a2_scene_nearest_chunk(r->scene, c->c.d.x, c->c.d.z);
    if (ch >= 0)
        c->chunk = ch;
    int wp = a2_aiw_nearest(r->w, c->chunk, c->c.d.x, c->c.d.y, c->c.d.z, 1);
    if (wp >= 0)
        c->wp = wp;
}

/* 0x409d10: drive the road from the racer's own waypoint */
static void st_drive(a2_racers *r, int i, racer *c, float dt)
{
    c->c.engine_off = 0;
    int old = c->twp;
    int t = a2_aiw_scan_path(r->w, c->wp, ORDER, c->near, c->far);
    c->twp = t;
    if (t > -1) {
        if (!a2_aiw_blocked(r->w, c->wp, A2_AI_ID_RACER + i, &c->c.d))
            a2_ai_speed_control(&c->c, c->far, 0, dt);
    } else {
        c->twp = old;
    }
    a2_ai_drive(&c->c, &r->env, c->near, band(r, i, c), 0.0f, 0, dt);
}

/* 0x409e00: the path ahead of the player's waypoint */
static void st_block(a2_racers *r, int i, racer *c, float dt)
{
    int old = c->twp;
    int t = a2_aiw_scan_path(r->w, r->player.wp, ORDER, c->near, c->far);
    c->twp = t;
    if (t > -1) {
        a2_ai_speed_control(&c->c, c->far, 0, dt);
    } else {
        c->c.brake = 0.0f;
        c->c.throttle = 0.0f;
        c->twp = old;
    }
    a2_ai_drive(&c->c, &r->env, c->near, band(r, i, c), 0.0f, 0, dt);
    c->timer += dt;
}

/* 0x40a290: far ahead, held to 60 km/h */
static void st_wait(a2_racers *r, int i, racer *c, float dt)
{
    int old = c->twp;
    int t = a2_aiw_scan_path(r->w, c->wp, ORDER, c->near, c->far);
    c->twp = t;
    if (t > -1)
        a2_ai_steer_to(&c->c, c->near);
    else
        c->twp = old;
    a2_ai_longitudinal(&c->c, &r->env, band(r, i, c), 0.0f, dt);
    a2_ai_steering(&c->c, &r->env, 0, dt);
    a2_ai_body(&c->c, &r->env, dt);
    c->c.d.v_fwd = 16.6667f; /* 0x418553f8 */
}

/* 0x40a190: back off at 15 m/s for a second */
static void st_unstick(racer *c, a2_racers *r, float dt)
{
    if (c->chunk_time >= 2.0f) {
        c->c.steer = 0.0f;
        c->c.d.v_fwd = -15.0f;
        c->chunk_time = 0.0f;
        c->rev_time = 0.0f;
        a2_ai_body(&c->c, &r->env, dt);
        return;
    }
    c->rev_time += dt;
    a2_ai_body(&c->c, &r->env, dt);
    if (c->rev_time > 1.0f) {
        c->state = ST_RACE;
        c->rev_time = 0.0f;
    }
}

/* 0x409ed0: finished -- engine off, the steering wound to 100 */
static void st_done(racer *c, a2_racers *r, float dt)
{
    c->c.engine_off = 1;
    c->c.steer += 75.0f * dt;
    if (c->c.steer > 100.0f)
        c->c.steer = 100.0f;
    c->c.throttle = 0.0f;
    c->c.brake = 0.0f;
    a2_ai_steering(&c->c, &r->env, 0, dt);
    a2_ai_body(&c->c, &r->env, dt);
}

/* 0x40a610: held by the police */
static void st_held(racer *c, a2_racers *r, float dt)
{
    c->c.throttle = 0.0f;
    c->c.brake = 0.0f;
    c->c.d.v_fwd = 0.0f;
    c->c.d.v_lat = 0.0f;
    c->c.d.yaw_rate = 0.0f;
    a2_ai_body(&c->c, &r->env, dt);
}

/* 0x409540: the time spent in the chunk, and progress */
static void track_chunk(a2_racers *r, racer *c, float dt)
{
    int ch = a2_scene_nearest_chunk(r->scene, c->c.d.x, c->c.d.z);
    if (ch < 0 || ch == c->chunk) {
        c->chunk_time += dt;
        return;
    }
    c->prev = c->chunk;
    c->chunk = ch;
    c->chunk_time = 0.0f;
    int d = a2_aiw_chunk_dist(r->w, c->chunk, c->prev);
    if (a2_aiw_ahead(r->w, c->chunk, c->prev))
        c->progress += d;
    else
        c->progress -= d;
}

void a2_racers_update(a2_racers *r, float dt, float race_time)
{
    if (!r)
        return;
    r->env.player_skid = r->player.skid;
    r->env.player_stopped = r->player.stopped;
    for (int i = 0; i < r->count; i++)
        if (r->cars[i].alive)
            transitions(r, &r->cars[i], dt);
    for (int i = 0; i < r->count; i++) {
        racer *c = &r->cars[i];
        if (!c->alive)
            continue;
        own_wp(r, c);
        switch (c->state) {
        case ST_RACE:
        case ST_BEHIND: st_drive(r, i, c, dt); break;
        case ST_BLOCK: st_block(r, i, c, dt); break;
        case ST_HELD: st_held(c, r, dt); break;
        case ST_DONE: st_done(c, r, dt); break;
        case ST_UNSTICK: st_unstick(c, r, dt); break;
        case ST_WAIT: st_wait(r, i, c, dt); break;
        default: break;
        }
        a2_ai_integrate(&c->c, dt);
        {
            /* 0x413be0 (0x40ae61 / 0x40ae79) */
            float gy;
            float gmy = height_fn(r, c->c.d.x, c->c.d.z, &gy) ? gy - c->c.d.y : 0.0f;
            a2_wheels_update_ai(&c->wheels, c->c.d.v_fwd, c->c.d.yaw_rate, gmy, dt);
        }
        track_chunk(r, c, dt);
        on_trigger(r, c, a2_aiw_trigger(r->w, c->chunk, 0), race_time);
        on_trigger(r, c, a2_aiw_trigger(r->w, c->chunk, 1), race_time);
    }
    {
        static int log = -1, frame = 0;
        if (log < 0)
            log = getenv("A2_AI_LOG") != NULL;
        if (log && ++frame % 30 == 0) {
            fprintf(stderr, "ai t=%.1f player ch %d wp %d prog %.0f rank %d |", race_time,
                    r->player.chunk, r->player.wp, r->player.progress, r->player.rank);
            for (int i = 0; i < r->count; i++) {
                const racer *c = &r->cars[i];
                fprintf(stderr, " [%d st%d ch%d v%.1f thr%.0f brk%.0f st%.0f g%d prog%d rk%d f%.2f]",
                        i, c->state, c->chunk, c->c.d.v_fwd, c->c.throttle, c->c.brake, c->c.steer,
                        c->c.d.gear, c->progress, c->rank, band(r, i, c));
            }
            fprintf(stderr, "\n");
        }
    }
    /* 0x408cb0: ranks, by progress (ties: the one further along the road) */
    for (int i = 0; i < r->count; i++) {
        racer *c = &r->cars[i];
        int ahead = 0;
        for (int j = 0; j < r->count; j++)
            if (j != i && r->cars[j].alive && r->cars[j].progress > c->progress)
                ahead++;
        if (r->player.progress_abs > (float)c->progress)
            ahead++;
        c->rank = ahead;
    }
}

/* ------------------------------------------------------------- the rest */

void a2_racers_draw(const a2_racers *r, const a2_scene *scene, int near_chunk, int radius)
{
    if (!r)
        return;
    for (int i = 0; i < r->count; i++) {
        const racer *c = &r->cars[i];
        if (!c->alive)
            continue;
        int d = a2_scene_chunk_delta(scene, near_chunk, c->chunk);
        if (d < -radius || d > radius)
            continue;
        /* the port's yaw (forward = (sin, -cos)) is half a turn from the
         * original's heading; meshes are drawn with 180 - yaw */
        float yaw = c->c.d.heading * (360.0f / 4096.0f) + 180.0f;
        a2_scene_draw_shadow(scene, c->model, c->c.d.x, c->c.d.z, c->chunk, 180.0f - yaw);
        glPushMatrix();
        glTranslatef(c->c.d.x, c->c.d.y, c->c.d.z);
        glRotatef(180.0f - yaw, 0.0f, 1.0f, 0.0f);
        /* the body's pitch and roll (0x40ece0 / 0x40ebd0), as the player's */
        glRotatef(c->c.d.pitch * (360.0f / 4096.0f), 1.0f, 0.0f, 0.0f);
        glRotatef(c->c.d.roll * (360.0f / 4096.0f), 0.0f, 0.0f, 1.0f);
        a2_scene_draw_model(scene, c->model, 0.0f, 0.0f, 0.0f, 0.0f);
        a2_wheels_draw(&c->wheels, scene, r->wheel_model, c->car);
        glPopMatrix();
    }
}

int a2_racers_count(const a2_racers *r) { return r ? r->count : 0; }

int a2_racers_car_xz(const a2_racers *r, int i, float out[2])
{
    if (!r || i < 0 || i >= r->count || !r->cars[i].alive)
        return 0;
    out[0] = r->cars[i].c.d.x;
    out[1] = r->cars[i].c.d.z;
    return 1;
}

float a2_racer_speed(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].c.d.v_fwd : 0.0f;
}

const char *a2_racer_name(const a2_racers *r, int i)
{
    if (!r || i < 0 || i >= r->count)
        return "?";
    return CAR_NAME[r->cars[i].car];
}

float a2_racer_progress(const a2_racers *r, int i)
{
    if (!r || i < 0 || i >= r->count)
        return 0.0f;
    return (float)r->cars[i].progress;
}

int a2_racers_position(const a2_racers *r, float player_progress)
{
    int ahead = 0;
    if (r)
        for (int i = 0; i < r->count; i++)
            /* A racer that has finished is ahead of a player who has not:
             * its progress stops at the finish, which the player's own count
             * can pass by a chunk on the frame it crosses. */
            if (r->cars[i].alive &&
                (r->cars[i].finished || a2_racer_progress(r, i) > player_progress))
                ahead++;
    return ahead + 1;
}

int a2_racers_collide(a2_racers *r, float *x, float *z, float radius, float push[2], int *who)
{
    int hit = 0;
    for (int i = 0; r && i < r->count; i++) {
        racer *c = &r->cars[i];
        if (c->alive && a2_ai_capsule_push(&c->c.d, a2_car_half_length(c->car),
                                           a2_car_half_width(c->car), x, z, radius, push)) {
            if (who)
                *who = i;
            hit = 1;
        }
    }
    return hit;
}

a2_drive *a2_racer_drive(a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count && r->cars[i].alive) ? &r->cars[i].c.d : NULL;
}

int a2_racer_finished(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].finished : 0;
}

float a2_racer_finish_time(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].finish_time : 0.0f;
}

int a2_racer_finish_position(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].finish_pos : 0;
}

int a2_racer_car(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].car : -1;
}

int a2_racers_sound_sources(const a2_racers *r, a2_sound_src *out, int max)
{
    int n = 0;
    for (int i = 0; r && i < r->count && n < max; i++) {
        const racer *c = &r->cars[i];
        if (!c->alive)
            continue;
        out[n].key = 1000 + i;
        out[n].x = c->c.d.x;
        out[n].z = c->c.d.z;
        out[n].anim = a2_car_anim_name(c->car);
        out[n].sample = -1;
        n++;
    }
    return n;
}

/* ------------------------------------------------- for the police (cops.c) */

int a2_racer_ai(const a2_racers *r, int i, a2_ai_target *t)
{
    if (!r || i < 0 || i >= r->count || !r->cars[i].alive)
        return 0;
    const racer *c = &r->cars[i];
    t->d = &c->c.d;
    t->chunk = c->chunk;
    t->wp = c->wp;
    t->limit = c->limit;
    t->state = c->state;
    return 1;
}

void a2_racer_arrest(a2_racers *r, int i)
{
    if (r && i >= 0 && i < r->count) {
        r->cars[i].state = ST_HELD;
        r->cars[i].timer = 0.0f;
    }
}

int a2_racer_state(const a2_racers *r, int i)
{
    return (r && i >= 0 && i < r->count) ? r->cars[i].state : -1;
}
