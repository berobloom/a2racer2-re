#include "drive.h"

#include <math.h>
#include <string.h>

#define TURN 4096.0f /* heading units per turn: the engine's sine table size */
#define TWO_PI 6.28318548f
#define HEAD_TO_RAD (TWO_PI / TURN) /* 0x484ad0 = 0.00153398 */

/* The MSVC rand() recurrence, for the suspension's road vibration. */
#ifdef A2_DRIVE_TEST
int a2_drive_test_rand = -1; /* >= 0: rand() returns this, for the diff test */
#endif

static int rand15(a2_drive *d)
{
#ifdef A2_DRIVE_TEST
    if (a2_drive_test_rand >= 0)
        return a2_drive_test_rand;
#endif
    d->rng = d->rng * 214013u + 2531011u;
    return (int)((d->rng >> 16) & 0x7fff);
}

void a2_drive_init(a2_drive *d, float x, float y, float z, float heading)
{
    memset(d, 0, sizeof *d);
    d->x = x;
    d->y = y;
    d->z = z;
    d->heading = heading;
    d->rpm = 800.0f;
    d->rng = 1;
}

float a2_drive_heading_rad(const a2_drive *d) { return d->heading * HEAD_TO_RAD; }

/* ------------------------------------------------------------------ input */

/* 0x4269c0, the stuur_model 0 arms (0x426b0e and 0x426c6d). */
void a2_drive_keys(a2_drive *d, unsigned keys, float dt)
{
    /* throttle: +70 per second up to 100 while UP is held, 0 on release */
    if (keys & A2_KEY_UP) {
        d->throttle += 70.0f * dt;
        if (d->throttle > 100.0f)
            d->throttle = 100.0f;
    } else {
        d->throttle = 0.0f;
    }
    /* brake: +40 per second up to 100, kept as a truncated integer */
    if (keys & A2_KEY_DOWN) {
        int b = (int)((float)d->brake + 40.0f * dt);
        d->brake = b > 100 ? 100 : b;
    } else {
        d->brake = 0;
    }
    /* steering: from centre it starts at +-15 and moves at 200 * k per
     * second with k = 2 * (1 - |s| / 100); the opposite key snaps it to 0
     * first; with neither key it returns to centre at 400 per second. */
    float s = d->steer;
    float k = (1.0f - fabsf(s) * 0.01f) * 2.0f;
    if (keys & A2_KEY_LEFT) {
        if (d->steer > 0.0f) {
            d->steer = 0.0f;
            return;
        }
        if (s == 0.0f)
            s = -15.0f;
        d->steer = s - dt * k * 200.0f;
        if (d->steer < -100.0f)
            d->steer = -100.0f;
    } else if (keys & A2_KEY_RIGHT) {
        if (d->steer < 0.0f) {
            d->steer = 0.0f;
            return;
        }
        if (s == 0.0f)
            s = 15.0f;
        d->steer = s + dt * k * 200.0f;
        if (d->steer > 100.0f)
            d->steer = 100.0f;
    } else {
        if (d->steer > 0.0f) {
            d->steer -= 400.0f * dt;
            if (d->steer < 0.0f)
                d->steer = 0.0f;
        }
        if (d->steer < 0.0f) {
            d->steer += 400.0f * dt;
            if (d->steer > 0.0f)
                d->steer = 0.0f;
        }
    }
}

/* -------------------------------------------------------------- the model */

/* 0x40e9f0: this gear's acceleration per throttle percent, the speed as a
 * fraction of the gear's top speed, and the engine rpm (800..9000 across the
 * gear).  The motor upgrade adds 4 m/s to every gear's top speed, the gearbox
 * upgrade selects the upgraded arrays; the turbo adds 0.05 and lifts the
 * nose. */
float a2_drive_gear_accel(a2_drive *d, const a2_drive_env *e, int gear, float *ratio, float dt)
{
    const a2_car *c = e->car;
    float vmax = (e->gearbox ? c->max_speed_upgraded : c->max_speed)[gear] + (e->motor ? 4.0f : 0.0f);
    float a = (e->gearbox ? c->accel_upgraded : c->accel)[gear];
    float r = d->v_fwd / vmax;
    *ratio = r;
    float acc = (1.0f - r) * a;
    d->rpm = 800.0f + fabsf(r) * 8200.0f;
    if (e->turbo_active) {
        if (d->v_fwd > 0.0f)
            d->pitch_w += 500.0f * dt;
        return acc + 0.05f;
    }
    return acc;
}

/* 0x40f0d0: coast, shift, accelerate and brake, on v_fwd directly. */
void a2_drive_longitudinal(a2_drive *d, const a2_drive_env *e, float throttle, float brake, float dt)
{
    const a2_car *c = e->car;
    int top = e->gearbox ? 5 : 4;
    /* coasting: the engine braking "Motor vertraging" pulls toward 0 */
    if (throttle == 0.0f) {
        if (d->v_fwd < 0.0f) {
            d->v_fwd += c->engine_decel * dt;
            if (d->v_fwd > 0.0f)
                d->v_fwd = 0.0f;
        } else {
            d->v_fwd -= c->engine_decel * dt;
            if (d->v_fwd < 0.0f)
                d->v_fwd = 0.0f;
        }
    }
    if (throttle > 0.0f)
        d->stopped = 0;
    float v0 = d->v_fwd;
    float ratio;
    float acc = a2_drive_gear_accel(d, e, d->gear, &ratio, dt);
    if (d->v_fwd > 1.0f) {
        if (ratio > c->upshift_point && d->gear < top) {
            d->gear++;
            acc = a2_drive_gear_accel(d, e, d->gear, &ratio, dt);
        }
        if (ratio < c->downshift_point && d->gear > 0) {
            d->gear--;
            acc = a2_drive_gear_accel(d, e, d->gear, &ratio, dt);
        }
    }
    float bk = c->brake_force;
    if (bk > 0.0f)
        bk = -bk;
    if (e->abs)
        bk -= 0.5f; /* ABS brakes harder, it does not prevent locking */
    if (d->handbrake) {
        acc = 0.0f;
        bk = 0.0f;
    }
    d->v_fwd = v0 + (acc * throttle + bk * brake) * dt;
    if (brake > 0.0f) {
        d->braking = 1;
        if (fabsf(d->v_fwd) < 0.4f && !d->stopped) {
            d->v_fwd = 0.0f;
            d->stopped = 1;
        }
        float dive = d->v_fwd * brake;
        if (dive > 1000.0f)
            dive = 1000.0f;
        d->pitch_w -= dt * dive * 0.5f; /* the nose dips under braking */
    } else {
        d->braking = 0;
    }
    if (d->v_fwd <= 0.0f) {
        d->braking = 0;
        d->rpm = 800.0f;
    }
    if (d->v_fwd < -13.333f)
        d->v_fwd = -13.333f;
}

/* Wrap an angle into [0, 2pi) the way 0x40f3c0 does it: one step either way. */
static float wrap1(float a)
{
    if (a > TWO_PI)
        return a - TWO_PI;
    if (a < 0.0f)
        return a + TWO_PI;
    return a;
}

/* 0x40f3c0: steering, grip, handbrake drifts and burnouts. */
void a2_drive_steering(a2_drive *d, const a2_drive_env *e, float steer, float throttle, float dt)
{
    const a2_car *c = e->car;
    /* the yaw rate is damped hard every frame unless a burnout is running */
    if (!d->handbrake && d->spin == 0.0f)
        d->yaw_rate -= d->yaw_rate * 10.0f * dt;

    if (d->handbrake && d->v_fwd == 0.0f && throttle > 80.0f) {
        /* a burnout on the spot: the car swings, more as the timer runs */
        float s = sinf(d->spin * TWO_PI);
        float kick = d->spin > 4.0f ? s * 0.4f : d->spin * 0.1f * s;
        d->spin += dt;
        d->yaw_rate += kick;
    } else {
        /* letting go of a burnout launches the car */
        if (d->spin != 0.0f && throttle > 0.0f) {
            float vmax1 = (e->gearbox ? c->max_speed_upgraded : c->max_speed)[1];
            d->v_fwd = d->spin * 0.25f * vmax1 * throttle * 0.01f;
            if (d->v_fwd > vmax1)
                d->v_fwd = vmax1;
        }
        d->spin = 0.0f;
    }

    if (!d->handbrake) {
        d->hb_heading = 0.0f;
        d->drift = 0.0f;
        /* lateral grip: the tyre upgrade doubles it */
        d->v_lat -= d->v_lat * (e->tyres ? 10.0f : 5.0f) * dt;
    }

    /* steering rate, faded in over the first 2 m/s */
    float rate = c->steer_speed;
    if (fabsf(d->v_fwd) < 2.0f && fabsf(d->v_lat) < 2.0f)
        rate = sinf(fabsf((float)M_PI * d->v_fwd * 0.25f)) * c->steer_speed;
    float sin_s = sinf(wrap1(steer * 0.5f * TWO_PI * (1.0f / 360.0f)));
    float dpsi = HEAD_TO_RAD * (rate * dt * steer + 50.0f * d->v_fwd * dt * sin_s);
    float dpsi_w = wrap1(dpsi);

    if (!d->handbrake && d->drift == 0.0f) {
        if (d->hb_flag && fabsf(d->v_lat) < 3.0f)
            d->hb_flag = 0;
        float grip = 10.0f - fabsf(d->v_fwd) * 0.15f;
        if (grip < 2.5f)
            grip = 2.5f;
        int roll_minus = 0;
        if (dpsi_w > 0.0f && dpsi_w < TWO_PI) {
            int skid = (int)fabsf(sinf(dpsi_w) * 20.0f);
            d->skid = skid > 10 ? 10 : skid;
            if (d->v_fwd < 10.0f && d->skid != 0) {
                d->skid = 5;
                if (d->v_fwd < 0.0f)
                    d->yaw_rate += grip * dpsi;
                else
                    d->yaw_rate -= grip * dpsi;
                d->v_fwd -= d->v_fwd * 2.0f * dt;
            } else {
                if (d->v_fwd < 0.0f)
                    d->yaw_rate += grip * dpsi;
                else
                    d->yaw_rate -= grip * dpsi;
                if (fabsf(steer) > 20.0f)
                    d->v_fwd -= (0.25f - d->v_fwd * 0.003f) * d->v_fwd * dt;
                roll_minus = 1;
            }
        } else {
            d->skid = 0;
            if (d->v_fwd < 0.0f)
                d->yaw_rate += d->v_fwd * -0.05f * grip * dpsi;
            else
                d->yaw_rate -= grip * dpsi;
        }
        /* CARS.DAT field 29 rolls the body into the turn */
        if (roll_minus)
            d->roll -= dt * c->roll_rate * steer;
        else
            d->roll += dt * c->roll_rate * steer;
        if (d->v_fwd < 1.0f)
            d->skid = 0;
        /* turn the velocity with the car: grip */
        float a = d->yaw_rate * dt;
        float nl = d->v_lat * cosf(a) - d->v_fwd * sinf(a);
        d->v_lat = e->tyres ? nl : nl * 0.8f;
        return;
    }

    /* the handbrake (0x40fa08): the car slides and the heading swings */
    d->hb_flag = 1;
    if (d->hb_heading == 0.0f)
        d->hb_heading = (float)(((int)d->heading & 0xfff)) * HEAD_TO_RAD;
    d->drift += dt;
    if (d->drift > 1.0f)
        d->drift = 1.0f;
    d->slide = sqrtf(d->v_lat * d->v_lat + d->v_fwd * d->v_fwd);
    if (d->slide > 1.0f) {
        float r = sinf(d->slide * (float)M_PI * 0.004f) * (d->drift * steer * 0.27f) * 0.5f;
        if (!(d->v_fwd < 0.0f && d->stopped))
            r = -r;
        d->yaw_rate = r;
        if (steer == 0.0f) {
            d->v_lat -= d->v_lat * 2.0f * dt;
            d->v_fwd -= d->v_fwd * 2.0f * dt;
        } else {
            d->v_lat -= d->v_lat * dt;
            d->v_fwd -= d->v_fwd * dt;
        }
        int skid = (int)(d->slide * 2.0f);
        d->skid = skid > 10 ? 10 : skid;
    } else {
        if (d->spin == 0.0f)
            d->skid = 0;
        d->v_lat = 0.0f;
        d->v_fwd = 0.0f;
        d->yaw_rate = 0.0f;
    }
}

/* 0x40ebd0 and 0x40ece0: the body's roll and pitch, damped springs from
 * CARS.DAT block B, shaken by road vibration of +-4 * v * dt, the sign
 * picked by rand() each frame.  The pitch
 * spring reads the roll factor (0x4a5df4), as the original does. */
static void suspension(a2_drive *d, const a2_drive_env *e, float dt)
{
    const a2_car *c = e->car;
    d->roll_w += (-c->roll_damping * d->roll_w - c->roll_factor * d->roll) * dt;
    d->roll += d->roll_w * dt;
    if (rand15(d) % 10 < 5)
        d->roll_w += 4.0f * d->v_fwd * dt;
    else
        d->roll_w -= 4.0f * d->v_fwd * dt;
    d->pitch_w += (-c->pitch_damping * d->pitch_w - c->roll_factor * d->pitch) * dt;
    d->pitch += d->pitch_w * dt;
    if (rand15(d) % 10 < 5)
        d->pitch_w += 4.0f * d->v_fwd * dt;
    else
        d->pitch_w -= 4.0f * d->v_fwd * dt;
}

/* 0x40ee80: the ride height, a spring toward the road just ahead. */
static void ride(a2_drive *d, const a2_drive_env *e, float dt)
{
    const a2_car *c = e->car;
    float h = d->heading * HEAD_TO_RAD;
    float cs = cosf(h), sn = sinf(h);
    float nx = d->x + 10.0f * dt * (cs * d->v_lat - sn * d->v_fwd);
    /* (sin + cos) * v_fwd, not sin * v_lat + cos * v_fwd: the original's own
     * slip (0x40ef2f..0x40ef3a), kept so the sample point matches. */
    float nz = d->z + 10.0f * dt * (sn + cs) * d->v_fwd;
    float road;
    if (!e->height || !e->height(e->height_ud, nx, nz, &road))
        return;
    /* both tests read the height from before this frame's update, as the
     * original does ([esp+0x1c], saved at 0x40eed9) */
    float y0 = d->y;
    d->v_y += (-c->height_damping * d->v_y - (y0 - road) * c->height_factor) * dt;
    d->y += d->v_y * dt;
    if (y0 < road - 0.5f) {
        d->y = road - 0.5f;
        d->v_y = -d->v_y;
    }
    if (y0 > road + 2.0f) {
        d->skid = 0;
        d->v_y -= 10.0f * dt; /* airborne */
    }
}

/* 0x4404a0: move along the old velocity, turn the heading, and rotate the
 * velocity back so it keeps its world direction. */
static void integrate(a2_drive *d, float dt)
{
    float h = floorf(d->heading);
    int hi = ((int)h) & 0xfff;
    float ang = (float)hi * HEAD_TO_RAD;
    float sn = sinf(ang), cs = cosf(ang);
    d->x += 10.0f * dt * (cs * d->v_lat - sn * d->v_fwd);
    d->z += 10.0f * dt * (sn * d->v_lat + cs * d->v_fwd);
    float dh = dt * d->yaw_rate * TURN / TWO_PI;
    d->heading += dh;
    int ri = ((int)(-dh)) & 0xfff; /* truncated, as __ftol does */
    float ra = (float)ri * HEAD_TO_RAD;
    float rs = sinf(ra), rc = cosf(ra);
    float nl = rc * d->v_lat - rs * d->v_fwd;
    float nf = rs * d->v_lat + rc * d->v_fwd;
    d->v_lat = nl;
    d->v_fwd = nf;
}

/* The original's collision response (session 13, fitted under Unicorn with
 * reverse-engineering/tools/emu_collide.py; see discoveries.md "The collision
 * impulse").  The car is a box (the vehicle table's length x width, metres)
 * with inverse mass M1 and inverse inertia I1 = 12 / (m (l^2 + w^2))
 * (0x4403b0).  `n` points from the car into what it hit; the contact is the
 * box corner deepest along n.  With v the contact point's relative velocity
 * (v + w x r, r in metres):
 *     j = -(1 + e) (v . n) / (M1 + M2)                0x448eb0 / 0x449570
 *     dv1 = M1 j n,  dv2 = -M2 j n,  dw1 = 0.34 I1 (r x n) j  (|w| <= 6)
 *     dv_y: 1.5 M2 j against an object, -1.5 M1 j against the world
 * e is the mean of the two objects' restitution, or the car's own against
 * the world.  Returns |j|, the impact magnitude (0x4e6788) the damage and
 * the crunch sound read, or 0 when the car is not closing.  *dv2 receives the
 * other body's velocity change (world, m/s). */
float a2_drive_impulse(a2_drive *d, float hl, float hw, float m1, float nx, float nz, float e,
                       float m2, float ovx, float ovz, int world, float dv2[3],
                       const float *opos, float ol, float ow, float cx, float cz)
{
    return a2_drive_impulse_at(d, hl, hw, m1, nx, nz, e, m2, ovx, ovz, world, dv2, opos, ol, ow,
                               cx, cz, NULL);
}

float a2_drive_impulse_at(a2_drive *d, float hl, float hw, float m1, float nx, float nz, float e,
                          float m2, float ovx, float ovz, int world, float dv2[3],
                          const float *opos, float ol, float ow, float cx, float cz,
                          const float *contact)
{
    if (dv2)
        dv2[0] = dv2[1] = dv2[2] = 0.0f;
    if (m1 <= 0.0f)
        return 0.0f;
    float M1 = 1.0f / m1, M2 = m2 > 0.0f ? 1.0f / m2 : 0.0f;
    float I1 = 12.0f / (m1 * (hl * hl * 4.0f + hw * hw * 4.0f));
    float h = d->heading * (6.28318548f / 4096.0f);
    float sn = sinf(h), cs = cosf(h);
    /* the integrator's frame: forward (-sn, cs), lateral (cs, sn) */
    float fx = -sn, fz = cs, lx = cs, lz = sn;
    float vx = cs * d->v_lat - sn * d->v_fwd, vz = sn * d->v_lat + cs * d->v_fwd;
    /* the box corner deepest along n, as the contact arm (metres) */
    float a = (fx * nx + fz * nz) >= 0.0f ? hl : -hl;
    float b = (lx * nx + lz * nz) >= 0.0f ? hw : -hw;
    float rx = a * fx + b * lx, rz = a * fz + b * lz;
    if (contact) { /* the engine's contact point, relative to us, in metres */
        rx = 0.1f * (contact[0] - cx);
        rz = 0.1f * (contact[1] - cz);
    }
    float cvx = vx - d->yaw_rate * rz - ovx, cvz = vz + d->yaw_rate * rx - ovz;
    float vn = cvx * nx + cvz * nz;
    if (vn <= 0.0f)
        return 0.0f; /* separating */
    float j = -(1.0f + e) * vn / (M1 + M2);
    vx += M1 * j * nx;
    vz += M1 * j * nz;
    d->v_lat = cs * vx + sn * vz;
    d->v_fwd = -sn * vx + cs * vz;
    d->yaw_rate += 0.34f * I1 * (rx * nz - rz * nx) * j;
    if (d->yaw_rate > 6.0f)
        d->yaw_rate = 6.0f;
    if (d->yaw_rate < -6.0f)
        d->yaw_rate = -6.0f;
    d->v_y += world ? -1.5f * M1 * j : 1.5f * M2 * j;
    if (dv2) {
        dv2[0] = -M2 * j * nx;
        dv2[1] = -M2 * j * nz;
        /* the other box's spin: -0.34 I2 (r2 x n) j, r2 from its centre to
         * the contact (metres), I2 = 12 / (m2 (l^2 + w^2)) */
        if (opos && m2 > 0.0f) {
            float px = cx + rx * 10.0f, pz = cz + rz * 10.0f; /* contact, units */
            float r2x = 0.1f * (px - opos[0]), r2z = 0.1f * (pz - opos[1]);
            float I2 = 12.0f / (m2 * (ol * ol + ow * ow));
            float w2 = -0.34f * I2 * (r2x * nz - r2z * nx) * j;
            dv2[2] = w2 > 6.0f ? 6.0f : w2 < -6.0f ? -6.0f : w2;
        }
    }
    return -j;
}

/* The body (0x40ebd0, 0x40ece0, 0x40ee80), which the AI drivers run after
 * their own longitudinal and steering steps, and the engine's integration,
 * which runs for every vehicle once the AI is done. */
void a2_drive_body(a2_drive *d, const a2_drive_env *e, float dt)
{
    suspension(d, e, dt);
    ride(d, e, dt);
}

void a2_drive_integrate(a2_drive *d, float dt) { integrate(d, dt); }

/* 0x40fc30, then the engine's integration. */
void a2_drive_step(a2_drive *d, const a2_drive_env *e, float dt)
{
    float throttle = d->throttle;
    if (e->turbo_active)
        throttle = 100.0f; /* the turbo holds the throttle wide open */
    while (d->heading < 0.0f)
        d->heading += TURN;
    while (d->heading >= TURN)
        d->heading -= TURN;
    /* game sequences force the handbrake on and the throttle off */
    if (d->handbrake && d->hb_forced && !e->forced_handbrake) {
        d->handbrake = 0;
        d->hb_forced = 0;
    }
    if (e->forced_handbrake) {
        d->handbrake = 1;
        d->throttle = 0.0f;
        throttle = 0.0f;
        d->hb_forced = 1;
    }
    float road = d->y;
    int have_road = e->height && e->height(e->height_ud, d->x, d->z, &road);
    if (!have_road || d->y - 5.0f <= road) {
        a2_drive_longitudinal(d, e, throttle, (float)d->brake, dt);
        a2_drive_steering(d, e, d->steer, throttle, dt);
        /* wheelspin in the low gears at more than 80% throttle */
        if (d->gear <= 1 && throttle > 80.0f)
            d->skid = (int)(throttle * 0.1f);
    } else {
        d->v_lat -= d->v_lat * 5.0f * dt;
        d->yaw_rate -= d->yaw_rate * 10.0f * dt;
        d->rpm = 4000.0f + 60.0f * throttle; /* free-revving in the air */
    }
    suspension(d, e, dt);
    ride(d, e, dt);
    if (!e->no_integrate)
        integrate(d, dt);
}

/* 0x4404a0 as 0x43f900 calls it: the move scaled by the contact time t (the
 * fraction of the step the car travels before it touches), the turn and the
 * velocity's rotation over the whole step. */
void a2_drive_integrate_t(a2_drive *d, float dt, float t)
{
    float h = floorf(d->heading);
    int hi = ((int)h) & 0xfff;
    float ang = (float)hi * HEAD_TO_RAD;
    float sn = sinf(ang), cs = cosf(ang);
    a2_drive_integrate_move(d, dt, 10.0f * dt * t * (cs * d->v_lat - sn * d->v_fwd),
                            10.0f * dt * t * (sn * d->v_lat + cs * d->v_fwd));
}

void a2_drive_integrate_move(a2_drive *d, float dt, float dx, float dz)
{
    d->x += dx;
    d->z += dz;
    float dh = dt * d->yaw_rate * TURN / TWO_PI;
    d->heading += dh;
    int ri = ((int)(-dh)) & 0xfff;
    float ra = (float)ri * HEAD_TO_RAD;
    float rs = sinf(ra), rc = cosf(ra);
    float nl = rc * d->v_lat - rs * d->v_fwd;
    float nf = rs * d->v_lat + rc * d->v_fwd;
    d->v_lat = nl;
    d->v_fwd = nf;
}

#ifdef A2_DRIVE_TEST
void a2_drive_test_suspension(a2_drive *d, const a2_drive_env *e, float dt) { suspension(d, e, dt); }
void a2_drive_test_ride(a2_drive *d, const a2_drive_env *e, float dt) { ride(d, e, dt); }
void a2_drive_test_integrate(a2_drive *d, float dt) { integrate(d, dt); }
#endif
