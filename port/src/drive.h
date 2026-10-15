/* The player car's driving model: the original's "new (A2 Racer II)" steering
 * model, Game.ini [OPTIONS] stuur_model = 0, the shipped setting.
 *
 * A transliteration of spel.dat's code, function by function:
 *   0x4269c0  keyboard input: throttle, brake and steering ramps
 *   0x40fc30  the per-frame step (ground test, forced handbrake, wheelspin)
 *   0x40f0d0  longitudinal: coasting, gears, accelerating, braking
 *   0x40e9f0  per-gear acceleration and the engine rpm
 *   0x40f3c0  steering, grip, handbrake slides and burnouts
 *   0x40ebd0 / 0x40ece0 / 0x40ee80  body roll, pitch and ride height
 *   0x4404a0  the engine's integrator (position, heading, velocity rotation)
 * It is driven by CARS.DAT (0x4a5ee0 + 124 * car), not by the 0x484B18
 * physics table: that table belongs to the old Autobahn Raser I model
 * (stuur_model = 1), which the shipped game never runs.  See "Two driving
 * models" in research/discoveries.md.
 *
 * Units are the original's: velocities in m/s in the car's frame (v_lat to the
 * side, v_fwd ahead), the heading in 4096 units per turn, positions in world
 * units (10 per metre), inputs 0..100 (steering -100..100, left negative).
 */
#ifndef A2_DRIVE_H
#define A2_DRIVE_H

#include "cars.h"

/* Road height under (x, z), in world units; returns 0 when there is none. */
typedef int (*a2_drive_height_fn)(void *ud, float x, float z, float *y);

typedef struct {
    /* vehicle record */
    float v_lat;    /* +0x6c */
    float v_y;      /* +0x70, vertical, world units */
    float v_fwd;    /* +0x74 */
    float pitch_w;  /* +0x78, body pitch rate */
    float yaw_rate; /* +0x7c, rad/s */
    float roll_w;   /* +0x80, body roll rate */
    /* the car's 3D object */
    float x, y, z;  /* +0x50 .. +0x58 */
    float pitch;    /* +0x74, body pitch, visual */
    float heading;  /* +0x78, 4096 units per turn */
    float roll;     /* +0x7c, body roll, visual */
    /* globals of the player's car */
    int gear;            /* 0x4a63e0 */
    float rpm;           /* 0x4a63d8 */
    int braking;         /* 0x4a63cc: the brake is applied (brake lights) */
    int skid;            /* 0x4a63dc: 0..10 skid intensity */
    int stopped;         /* 0x4a63e4: latched at a standstill under braking */
    float spin;          /* 0x4a6484: handbrake burnout timer */
    float drift;         /* 0x4a6480: handbrake drift, 0..1 */
    float slide;         /* 0x4a6488: sliding speed during a handbrake drift */
    float hb_heading;    /* 0x4a6490 */
    int hb_flag;         /* 0x4a6494 */
    int handbrake;       /* 0x4d9df8 */
    int hb_forced;       /* 0x4a6498: handbrake forced by a game sequence */
    /* inputs, as 0x4269c0 keeps them */
    float throttle;      /* 0x4d9cdc, 0..100 */
    int brake;           /* 0x4d9e08, 0..100 */
    float brake_ramp;    /* port: the brake ramp with its fraction kept */
    float steer;         /* 0x4d9cd8, -100..100 */
    unsigned rng;
} a2_drive;

typedef struct {
    const a2_car *car;   /* CARS.DAT record */
    int motor, gearbox, tyres, abs; /* Game.ini [PLAYER] upgrades */
    int turbo_active;    /* 0x4d9dfc */
    int forced_handbrake; /* game mode 8, 6 or 3 (chase, game over, victory) */
    int no_integrate;     /* leave the move to the caller (a2_drive_integrate_t) */
    a2_drive_height_fn height;
    void *height_ud;
} a2_drive_env;

/* Key bits as the original's input mask (0x4246c0). */
enum { A2_KEY_LEFT = 1, A2_KEY_RIGHT = 2, A2_KEY_UP = 4, A2_KEY_DOWN = 8 };

void a2_drive_init(a2_drive *d, float x, float y, float z, float heading);
/* 0x4269c0, the keyboard: ramps the throttle, brake and steering. */
void a2_drive_keys(a2_drive *d, unsigned keys, float dt);
/* 0x40fc30 and its callees, then 0x4404a0's integration. */
void a2_drive_step(a2_drive *d, const a2_drive_env *e, float dt);
/* 0x40ebd0 / 0x40ece0 / 0x40ee80, and 0x4404a0, alone, for the AI drivers. */
void a2_drive_body(a2_drive *d, const a2_drive_env *e, float dt);
/* 0x4404a0 with a contact time: move t of the step's way, turn the whole step. */
void a2_drive_integrate_t(a2_drive *d, float dt, float t);
/* The same with the step's displacement given (world units): the move along
 * an obstacle after a contact. */
void a2_drive_integrate_move(a2_drive *d, float dt, float dx, float dz);
void a2_drive_integrate(a2_drive *d, float dt);
/* The engine's collision impulse (0x441490's response, see main.c's notes):
 * the car `d` (half-length hl, half-width hw in metres, mass m1) hits along
 * the normal (nx, nz) a body of mass m2 moving at (ovx, ovz), restitution e
 * (`world` for a barrier).  Updates d's velocity, spin and hop; returns |j|;
 * dv2 receives the other body's velocity change and spin. */
float a2_drive_impulse(a2_drive *d, float hl, float hw, float m1, float nx, float nz, float e,
                       float m2, float ovx, float ovz, int world, float dv2[3],
                       const float *opos, float ol, float ow, float cx, float cz);
/* The same with the contact point given (world units), as the engine's
 * contact test finds it (contact.h); NULL falls back to the box corner
 * deepest along n. */
float a2_drive_impulse_at(a2_drive *d, float hl, float hw, float m1, float nx, float nz, float e,
                          float m2, float ovx, float ovz, int world, float dv2[3],
                          const float *opos, float ol, float ow, float cx, float cz,
                          const float *contact);
/* The heading in radians, and the forward direction in world (x, z). */
float a2_drive_heading_rad(const a2_drive *d);

#endif /* A2_DRIVE_H */
