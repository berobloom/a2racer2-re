/* The CPU racers you are actually racing against.
 *
 * These are distinct from `traffic.c`: traffic exists to be overtaken and is
 * recycled once you leave it behind, whereas a racer runs the whole route and
 * has a finishing position.  Game.ini's `[RACERS]` section stores exactly that
 * per opponent -- "Arg 0: Zero based index of car the CPU racer had. Arg 1:
 * Zero based position the CPU racer had when finished. Arg 2: Finish time of
 * the CPU racer in msecs" -- with `count=3` in the shipped file.
 *
 * Each racer is drawn with its own slot's `RACER_<n+1>_ANIM` mesh, chosen
 * next to the player's car as the original chooses them (see cars.h).
 *
 * The driving is the shipped game's AI, Game.ini ai_model = 0 (session 13;
 * the earlier port followed HANDLE_RACER_AI, which only ai_model = 1 runs).
 * A racer is record 0-2 of the table at 0x4a55a0:
 *  - the transition table at 0x4a5410: 0 race, 1 the player is close behind
 *    the leader, 2 cut in front of the player, 7 held by the police (4 s),
 *    9 finished, 10 unstick (back off at 15 m/s), 11 far ahead (60 km/h);
 *  - it follows the .dmp waypoint graph by ScanPath (ai_world.h), avoiding
 *    waypoints other vehicles are on, and drives a physical car by throttle,
 *    brake and steering (ai_car.h) -- **the player's car**, with the player's
 *    upgrades, scaled by the rubber band 0x4014e0;
 *  - it does not obey speed limits; its limit (+0x2c) only decides whether a
 *    cop chases it;
 *  - it finishes on trigger 14 (on a circuit after 15 and Nrlaps laps).
 * See research/discoveries.md, "The new AI model, traced".
 *
 * The grid is the original's: one car a chunk back from STARTCHUNK, in the
 * player's lane, the racers in the places the player does not hold.
 * Ranking ties (same chunk count) are split by 0x408780: along the waypoint
 * the first car is on, is the other in front of it?
 */
#ifndef A2_RACERS_H
#define A2_RACERS_H

#include "ai_car.h"
#include "ai_world.h"
#include "anims.h"
#include "archive.h"
#include "audio.h"
#include "cars.h"
#include "render.h"

typedef struct a2_racers a2_racers;

typedef struct {
    int count;            /* Game.ini [RACERS] count */
    int player_slot;      /* the player's car, for choosing the racers' meshes */
    const a2_car *player_car; /* the car they drive: the player's */
    int gearbox, tyres;   /* the player's upgrades */
    int difficulty;       /* 0 EASY, 1 HARD */
    int etappe;           /* 0..6 */
    int circuit, laps;    /* city circuit and Nrlaps */
    int demo;             /* Game.ini demo */
    int start_chunk;      /* STARTCHUNK */
    int player_position;  /* the player's grid place, 0..3 (0x4d9e60) */
} a2_racers_config;

/* Returns NULL if no racer model could be loaded. */
a2_racers *a2_racers_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                            const a2_cars *cars, const a2_aiw *w, const a2_racers_config *cfg);
void a2_racers_free(a2_racers *r);

/* Each frame: the player as the AI sees it, the racers into the obstacle
 * lists, then the update (transitions, states, the integrator, chunks and
 * triggers, ranks). */
void a2_racers_set_player(a2_racers *r, const a2_ai_player *p);
void a2_racers_register(const a2_racers *r, a2_aiw *w);
void a2_racers_update(a2_racers *r, float dt, float race_time);

int a2_racer_finished(const a2_racers *r, int i);
float a2_racer_finish_time(const a2_racers *r, int i);
/* 1-based finishing position, or 0 if still running. */
int a2_racer_finish_position(const a2_racers *r, int i);
int a2_racer_car(const a2_racers *r, int i);

/* The racers as sound sources (keys 1000+); see a2_traffic_sound_sources. */
int a2_racers_sound_sources(const a2_racers *r, a2_sound_src *out, int max);
/* Draw the racers within `radius` chunks of `near_chunk`. */
void a2_racers_draw(const a2_racers *r, const a2_scene *scene, int near_chunk, int radius);

int a2_racers_count(const a2_racers *r);

/* World position of racer `i`, for the radar.  Returns 0 if it is not running. */
int a2_racers_car_xz(const a2_racers *r, int i, float out[2]);
float a2_racer_speed(const a2_racers *r, int i); /* m/s */
const char *a2_racer_name(const a2_racers *r, int i);

/* Chunks from the start of lap 1, the quantity race position is ranked on. */
float a2_racer_progress(const a2_racers *r, int i);

/* The player's 1-based position given their own progress in the same units. */
int a2_racers_position(const a2_racers *r, float player_progress);

/* Collide a car at (x, z) with the racers, as a2_traffic_collide does:
 * pushes it out and gives the normal and the racer hit.  The impulse is the
 * caller's, on the racer's car (a2_racer_drive). */
int a2_racers_collide(a2_racers *r, float *x, float *z, float radius, float push[2], int *who);
a2_drive *a2_racer_drive(a2_racers *r, int i);

/* For the police: racer `i` as a chase target, an arrest (state 7), and its
 * state. */
int a2_racer_ai(const a2_racers *r, int i, a2_ai_target *t);
void a2_racer_arrest(a2_racers *r, int i);
int a2_racer_state(const a2_racers *r, int i);

#endif /* A2_RACERS_H */
