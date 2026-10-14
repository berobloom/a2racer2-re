/* The police, on the shipped game's AI (Game.ini ai_model = 0).
 *
 * Session 13 rebuilt this module: the earlier port followed HANDLE_COP_AI's
 * state machine (lookout, pursuit, "the catch is a block one chunk ahead"),
 * which only ai_model = 1 runs.  What the shipped game does, all from
 * research/discoveries.md "The new AI model, traced":
 *
 *  - The cops are the type-9 .omp placements.  A placed cop with no chase
 *    is parked (0x40fba0 damps it to a stop).
 *  - **A chase starts in the cop's own chunk** (0x409060): a racer there at
 *    or above its record +0x2c limit, or the player there above 12.5 / 17.5 /
 *    25 / 30 m/s for the speed trigger 4 / 5 / 6 / 7 found back from the cop's
 *    chunk (trigger 8, the posted 120, sets none).  At most 1 cop on the
 *    player on EASY, 3 on HARD, 2 on a racer; on a city stage only with a
 *    chance of about dt a frame (0x4034a0).
 *  - The chasing cop is a record 3-14 of the table at 0x4a55a0, run by the
 *    transition table at 0x4a5280 (0x409200): 3 drive the road flat out
 *    (+0.1 acceleration), 4 the target is behind -- move into its lane, brake
 *    at 50 when it is slower and at 100 when it is right behind, 5 caught
 *    (the target has stopped within 71 units), 6 stand 4 s, 17 drive off
 *    at 10 m/s for 3 s, 8 coast and retire beyond 30 chunks, 14 / 16 the
 *    target turned round / is a ghost driver, 15 gave up (after 30 s, 60
 *    on HARD, or 50 chunks apart), 9 retired at the finish (highway), 10
 *    unstick.
 *  - A catch of the player is the game's mode 8 (set_game_mode(8) at
 *    0x40a3d4): the warnings and the arrest that main.c already does
 *    (g_times_caught, 49 / 50, then 42 and 43).  A caught racer stands for
 *    4 s (racer state 7).
 *  - The cop drives the player's car, by the same ScanPath and vehicle as
 *    the racers (ai_world.h, ai_car.h).
 */
#ifndef A2_COPS_H
#define A2_COPS_H

#include "ai_car.h"
#include "ai_world.h"
#include "anims.h"
#include "archive.h"
#include "audio.h"
#include "racers.h"
#include "render.h"
#include "track.h"

typedef struct a2_cops a2_cops;

typedef struct {
    const a2_car *player_car; /* the car they drive: the player's */
    int gearbox, tyres;       /* the player's upgrades */
    int difficulty;           /* 0 EASY, 1 HARD */
    int circuit;              /* city stage (Raser.ini track flag) */
    int demo;
} a2_cops_config;

a2_cops *a2_cops_create(a2_scene *scene, const a2_archive *rcs, const a2_anims *anims,
                        const a2_track *track, const a2_aiw *w, const a2_cops_config *cfg);
void a2_cops_free(a2_cops *c);

/* Each frame, after a2_racers_set_player: the cops into the obstacle lists,
 * then the update.  Returns 1 on the frame a cop catches the player. */
void a2_cops_register(const a2_cops *c, a2_aiw *w);
int a2_cops_update(a2_cops *c, const a2_ai_player *player, a2_racers *racers, float dt);

/* The cops as sound sources (keys 3000 + i): the siren (40) while it is on,
 * otherwise -1 for the caller to resolve to its engine note.  Parked cops
 * are silent. */
int a2_cops_sound_sources(const a2_cops *c, a2_sound_src *out, int max);
void a2_cops_draw(const a2_cops *c, const a2_scene *scene, int near_chunk, int radius);

int a2_cops_times_caught(const a2_cops *c);
/* Non-zero while a siren is on. */
int a2_cops_in_pursuit(const a2_cops *c);
/* The language.ini id of the banner to show, or 0.  `*age` is its age in
 * seconds so the caller can fade it out. */
int a2_cops_message(const a2_cops *c, float *age);
/* World position of cop `i`, for the radar.  Returns 0 if it is not out. */
int a2_cops_car_xz(const a2_cops *c, int i, float out[2]);
int a2_cops_count(const a2_cops *c);
/* The radio line due this frame, then cleared: 1 a chase began (0x4150f0,
 * 13 lines), 2 one was given up (0x415170, 5 lines); 0 none.  Always for the
 * player, one time in three for a racer, as the original rolls it. */
int a2_cops_take_voice(a2_cops *c);

/* Collide a car at (x, z) with the cops, parked or driving, as
 * a2_racers_collide does. */
int a2_cops_collide(a2_cops *c, float *x, float *z, float radius, float push[2], int *who);
a2_drive *a2_cop_drive(a2_cops *c, int i);

/* The police car's half width and length (its mesh), world units. */
void a2_cop_half_size(float *half_wid, float *half_len);

#endif /* A2_COPS_H */
