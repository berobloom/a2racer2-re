/* The new AI model's vehicle (Game.ini ai_model = 0, the shipped setting):
 * how a racer or a chasing cop drives its car.
 *
 * A transliteration of spel.dat, function by function:
 *   0x40eae0  per-gear acceleration, scaled by the rubber band
 *   0x4014e0  the rubber band
 *   0x40ff90  longitudinal: coast, gears, throttle and brake
 *   0x410280  steering, grip and the engine-off slide
 *   0x4012c0  speed control toward a point (throttle, brake, or a cop's speed)
 *   0x401230  steering toward a point
 *   0x401690 / 0x40fe90  the bearing of a point, 0..4096 / -2048..2048
 * and then the player's own 0x40ebd0 / 0x40ece0 / 0x40ee80 body and the
 * engine's integrator 0x4404a0 (drive.c), which the caller runs once the
 * frame's AI is done, as the engine does.
 *
 * The model is a copy of the player's (0x40f0d0 / 0x40f3c0) with differences
 * that are kept here as the original has them -- notably it drives **the
 * player's car**: CARS.DAT is indexed by [0x4d9e2c], the player's car type,
 * with the player's gearbox and tyre upgrades.  See research/discoveries.md,
 * "The new AI model, traced". */
#ifndef A2_AI_CAR_H
#define A2_AI_CAR_H

#include "drive.h"

typedef struct {
    a2_drive d;       /* position, heading, velocities, body; d.gear is +0x44 */
    float steer;      /* record +0x34, -100..100 */
    float throttle;   /* +0x38, 0..100 */
    float brake;      /* +0x3c, 0..100 */
    int engine_off;   /* +0x40 */
    float drift;      /* +0x48, 0..1 */
    float drift_sum;  /* +0x4c */
    float drift_head; /* +0x50 */
    int stopped;      /* +0x58 */
} a2_ai_car;

typedef struct {
    const a2_car *car;  /* the player's CARS.DAT record */
    int gearbox, tyres; /* the player's upgrades (0x4d9ed0, 0x4d9ec4) */
    int player_skid;    /* 0x4a63dc, read by the low-speed steering branch */
    int player_stopped; /* 0x4a63e4, read by the slide */
    int demo;           /* Game.ini [OPTIONS] demo (0x4d9b20) */
    a2_drive_height_fn height;
    void *height_ud;
} a2_ai_env;

/* 0x4014e0: the factor for racer `racer` (0..2) with `lead` chunks over the
 * player, on `etappe` 0..6 at `difficulty` 0 (EASY) / 1 (HARD).  1 for
 * anything else (cops). */
float a2_ai_rubber_band(int racer, int lead, int difficulty, int etappe);

/* 0x40ff90 with 0x40eae0: `factor` from a2_ai_rubber_band (the demo clamp is
 * applied here), `bonus` 0.1 for a cop in state 3. */
void a2_ai_longitudinal(a2_ai_car *c, const a2_ai_env *e, float factor, float bonus, float dt);
/* 0x410280; `cop` selects the cops' slide terms. */
void a2_ai_steering(a2_ai_car *c, const a2_ai_env *e, int cop, float dt);
/* 0x40ebd0, 0x40ece0, 0x40ee80: the body's roll, pitch and ride height. */
void a2_ai_body(a2_ai_car *c, const a2_ai_env *e, float dt);
/* The engine's integrator 0x4404a0, run after every AI step of the frame. */
void a2_ai_integrate(a2_ai_car *c, float dt);

/* 0x401690: bearing of (x, z) from the car, 0 = dead ahead, 2048 = behind. */
int a2_ai_bearing(const a2_drive *d, float x, float z);
/* 0x40fe90: the same, -2048..2048. */
int a2_ai_rel_angle(const a2_drive *d, float x, float z);

/* 0x4012c0: throttle up toward 70 - |angle|/15 m/s, or brake (a racer) /
 * take that speed (a cop) above it. */
void a2_ai_speed_control(a2_ai_car *c, const float far[3], int cop, float dt);
/* 0x401230: steer = angle / 2, saturating at +-100. */
void a2_ai_steer_to(a2_ai_car *c, const float near[3]);
/* 0x4013c0: heading wrap, longitudinal, steer toward `near`, steering and the
 * body, as the drive states do it. */
void a2_ai_drive(a2_ai_car *c, const a2_ai_env *e, const float near[3], float factor,
                 float bonus, int cop, float dt);

/* A collision's share for an AI car: its world velocity (m/s), and a world
 * velocity change plus a yaw-rate change applied to it. */
void a2_ai_world_velocity(const a2_drive *d, float *vx, float *vz);
void a2_ai_push(a2_drive *d, float dvx, float dvz, float dw);

/* Push a car of half-width `radius` at (*x, *z) out of an AI car's capsule
 * (its body axis, half_len - half_wid each way, widened by half_wid), as the
 * port's traffic does.  Returns 1 on contact, with the outward normal. */
int a2_ai_capsule_push(const a2_drive *d, float half_len, float half_wid, float *x, float *z,
                       float radius, float push[2]);

#endif /* A2_AI_CAR_H */
