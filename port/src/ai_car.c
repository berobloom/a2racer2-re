#include "ai_car.h"

#include <math.h>

#define TURN 4096.0f
#define TWO_PI 6.28318548f
#define HEAD_TO_RAD (TWO_PI / TURN) /* 0x484ad0 */

/* ------------------------------------------------------- the rubber band */

/* 0x482258: {upper, lower, gain} by (difficulty * 7 + etappe); the original
 * repeats each row for its three racers. */
static const float RUBBER[2][7][3] = {
    {{25, -25, 1.0f}, {25, -25, 1.5f}, {30, -20, 2.0f}, {30, -20, 2.5f},
     {35, -15, 3.0f}, {35, -15, 3.0f}, {45, -10, 4.0f}},
    {{35, -20, 2.0f}, {35, -20, 2.5f}, {40, -15, 3.0f}, {40, -15, 3.5f},
     {45, -10, 4.0f}, {45, -5, 4.5f}, {50, -5, 5.0f}},
};
/* the locals at 0x4014eb..0x401543, by racer */
static const float AHEAD[2][3] = {{0.95f, 0.92f, 0.90f}, {1.02f, 1.01f, 1.00f}};
static const float BEHIND[2][3] = {{1.00f, 0.98f, 0.95f}, {1.15f, 1.10f, 1.05f}};

float a2_ai_rubber_band(int racer, int lead, int difficulty, int etappe)
{
    if (racer < 0 || racer >= 3)
        return 1.0f;
    int hard = difficulty ? 1 : 0;
    if (etappe < 0)
        etappe = 0;
    if (etappe > 6)
        etappe = 6;
    const float *row = RUBBER[hard][etappe];
    float l = (float)lead;
    float f;
    if (l > 0.0f) {
        if (l > 50.0f)
            l = 50.0f;
        if (l > row[0])
            f = 1.0f - l * row[2] * 0.02f;
        else
            f = AHEAD[hard][racer];
    } else {
        if (l < -50.0f)
            l = -50.0f;
        if (l < row[1])
            f = 1.0f - l * row[2] * 0.02f;
        else
            f = BEHIND[hard][racer];
    }
    if (f < 0.2f)
        f = 0.2f;
    if (f > 2.0f)
        f = 2.0f;
    return f;
}

/* -------------------------------------------------------- longitudinal */

/* 0x40eae0: the gear's top speed and acceleration both scale with the band. */
static float gear_accel(const a2_ai_car *c, const a2_ai_env *e, float f, float bonus,
                        float *ratio)
{
    const a2_car *car = e->car;
    int g = c->d.gear;
    float vmax = (e->gearbox ? car->max_speed_upgraded : car->max_speed)[g];
    float a = (e->gearbox ? car->accel_upgraded : car->accel)[g];
    float r = c->d.v_fwd / (vmax * f);
    *ratio = r;
    float acc = f * ((1.0f - r) * a);
    if (acc < 0.0f)
        acc = 0.0f;
    return acc + bonus;
}

void a2_ai_longitudinal(a2_ai_car *c, const a2_ai_env *e, float factor, float bonus, float dt)
{
    const a2_car *car = e->car;
    a2_drive *d = &c->d;
    if (e->demo && factor < 1.0f)
        factor = 1.0f;
    float thr = c->throttle, brk = c->brake;
    int gear0 = d->gear;
    int top = e->gearbox ? 5 : 4;
    if (thr == 0.0f) {
        if (d->v_fwd < 0.0f) {
            d->v_fwd += car->engine_decel * dt;
            if (d->v_fwd > 0.0f)
                d->v_fwd = 0.0f;
        } else {
            d->v_fwd -= car->engine_decel * dt;
            if (d->v_fwd < 0.0f)
                d->v_fwd = 0.0f;
        }
    }
    if (thr > 0.0f)
        c->stopped = 0;
    float v0 = d->v_fwd;
    float ratio;
    float acc = gear_accel(c, e, factor, bonus, &ratio);
    if (d->v_fwd > 1.0f) {
        /* both tests compare the gear the frame started with (0x410102,
         * 0x410146), the second against the ratio after any upshift */
        if (ratio > car->upshift_point && gear0 < top) {
            d->gear++;
            acc = gear_accel(c, e, factor, bonus, &ratio);
        }
        if (ratio < car->downshift_point && gear0 > 0) {
            d->gear--;
            acc = gear_accel(c, e, factor, bonus, &ratio);
        }
    }
    float bk = car->brake_force;
    if (bk > 0.0f)
        bk = -bk;
    if (c->engine_off) {
        acc = 0.0f;
        bk = 0.0f;
    }
    d->v_fwd = v0 + (thr * acc + brk * bk) * dt;
    if (brk > 0.0f) {
        if (fabsf(d->v_fwd) < 0.4f && !c->stopped) {
            d->v_fwd = 0.0f;
            c->stopped = 1;
        }
        float dive = d->v_fwd * brk;
        if (dive > 1000.0f)
            dive = 1000.0f;
        d->pitch_w -= dt * dive; /* not halved, unlike the player's */
    }
    if (d->v_fwd < -13.333f)
        d->v_fwd = -13.333f;
}

/* ------------------------------------------------------------ steering */

static float wrap1(float a)
{
    if (a > TWO_PI)
        return a - TWO_PI;
    if (a < 0.0f)
        return a + TWO_PI;
    return a;
}

void a2_ai_steering(a2_ai_car *c, const a2_ai_env *e, int cop, float dt)
{
    const a2_car *car = e->car;
    a2_drive *d = &c->d;
    float steer = -c->steer; /* 0x4102a7: negated here, restored at the end */
    float lat_k = 0.0f;
    if (!c->engine_off) {
        d->yaw_rate -= d->yaw_rate * 10.0f * dt;
        c->drift_sum = 0.0f;
        c->drift_head = 0.0f;
        c->drift = 0.0f;
        lat_k = e->tyres ? 10.0f : 5.0f;
        lat_k *= d->v_lat;
        d->v_lat -= lat_k * dt;
    }
    float vabs = fabsf(d->v_fwd);
    float rate = car->steer_speed;
    if (vabs < 2.0f && fabsf(d->v_lat) < 2.0f)
        rate = sinf(fabsf((float)M_PI * d->v_fwd * 0.25f)) * car->steer_speed;
    float sin_s = sinf(wrap1(steer * 0.5f * TWO_PI * (1.0f / 360.0f)));
    float dpsi = HEAD_TO_RAD * (rate * dt * steer + 50.0f * d->v_fwd * dt * sin_s);
    float dpsi_w = wrap1(dpsi);
    int hi = (int)d->heading;
    hi = hi < 0 ? -((-hi) & 0xfff) : (hi & 0xfff);
    if (hi < 0)
        hi += 0x1000;
    float head_rad = (float)hi * (1.0f / 4096.0f) * TWO_PI;

    if (!c->engine_off && c->drift == 0.0f) {
        float grip = 10.0f - vabs * 0.15f;
        if (grip < 2.5f)
            grip = 2.5f;
        if (dpsi_w > 0.0f && dpsi_w < TWO_PI) {
            if (d->v_fwd < 10.0f && e->player_skid != 0) {
                if (d->v_fwd < 0.0f)
                    d->yaw_rate += dpsi * grip;
                else
                    d->yaw_rate -= dpsi * grip;
                d->roll += dt * car->roll_rate * steer;
            } else {
                if (d->v_fwd < 0.0f)
                    d->yaw_rate += dpsi * grip;
                else
                    d->yaw_rate -= dpsi * grip;
                d->roll -= dt * car->roll_rate * steer;
            }
        } else {
            if (d->v_fwd < 0.0f)
                d->yaw_rate -= steer * -0.004f;
            else
                d->yaw_rate -= dpsi * grip;
            d->roll += dt * car->roll_rate * steer;
            d->v_fwd -= lat_k * dt * 0.5f;
        }
        float a = d->yaw_rate * dt;
        float nl = d->v_lat * cosf(a) - sinf(a) * d->v_fwd;
        d->v_lat = e->tyres ? nl : nl * 0.8f;
        return;
    }

    /* 0x4106ff: the engine-off slide */
    if (c->drift_head == 0.0f)
        c->drift_head = head_rad;
    c->drift += dt;
    if (c->drift > 1.0f)
        c->drift = 1.0f;
    float slide = sqrtf(d->v_lat * d->v_lat + d->v_fwd * d->v_fwd);
    if (slide > 1.0f) {
        float r = sinf(slide * (float)M_PI * 0.004f) * (c->drift * steer * 0.18f);
        r *= cop ? 1.25f : 0.5f;
        float yr = r;
        if (!(d->v_fwd < 0.0f && e->player_stopped))
            yr = -yr;
        d->yaw_rate = yr;
        c->drift_sum += dt * r;
        if (!cop) {
            if (steer != 0.0f) {
                d->v_lat -= d->v_lat * dt;
                d->v_fwd -= d->v_fwd * dt;
            } else {
                d->v_lat -= d->v_lat * 2.0f * dt;
                d->v_fwd -= d->v_fwd * 2.0f * dt;
            }
        } else {
            d->v_lat -= d->v_lat * 3.0f * dt;
            d->v_fwd -= d->v_fwd * 3.0f * dt;
        }
    } else {
        d->v_lat = 0.0f;
        d->v_fwd = 0.0f;
        d->yaw_rate = 0.0f;
    }
}

void a2_ai_body(a2_ai_car *c, const a2_ai_env *e, float dt)
{
    a2_drive_env de = {0};
    de.car = e->car;
    de.gearbox = e->gearbox;
    de.tyres = e->tyres;
    de.height = e->height;
    de.height_ud = e->height_ud;
    a2_drive_body(&c->d, &de, dt);
}

void a2_ai_integrate(a2_ai_car *c, float dt) { a2_drive_integrate(&c->d, dt); }

/* ------------------------------------------------------------- bearings */

static float atan_units(const a2_drive *d, float x, float z)
{
    float dx = x - d->x;
    float dz = z - d->z;
    if (dx > 0.0f && dx < 0.1f)
        dx = 0.1f;
    if (dx < 0.0f && dx > -0.1f)
        dx = -0.1f;
    float a = atanf(dz / dx) * 0.159155f * 4096.0f;
    a += dx > 0.0f ? 3072.0f : 1024.0f;
    return a - d->heading;
}

int a2_ai_bearing(const a2_drive *d, float x, float z)
{
    float a = atan_units(d, x, z);
    if (a > 4096.0f)
        a -= 4096.0f;
    if (a < 0.0f)
        a += 4096.0f;
    return (int)a;
}

int a2_ai_rel_angle(const a2_drive *d, float x, float z)
{
    float a = atan_units(d, x, z);
    if (a > 2048.0f)
        a -= 4096.0f;
    if (a < -2048.0f)
        a += 4096.0f;
    return (int)a;
}

/* -------------------------------------------------------------- control */

void a2_ai_speed_control(a2_ai_car *c, const float far[3], int cop, float dt)
{
    int b = a2_ai_rel_angle(&c->d, far[0], far[2]);
    if (b < 0)
        b = -b;
    if (b > 1000)
        b = 500;
    float want = (float)(70 - b / 15);
    if (want > c->d.v_fwd) {
        c->throttle += 75.0f * dt;
        if (c->throttle > 100.0f)
            c->throttle = 100.0f;
        c->brake = 0.0f;
    } else if (!cop) {
        c->brake += 400.0f * dt;
        if (c->brake > 100.0f)
            c->brake = 100.0f;
    } else {
        c->d.v_fwd = want;
    }
}

void a2_ai_steer_to(a2_ai_car *c, const float near[3])
{
    int a = a2_ai_rel_angle(&c->d, near[0], near[2]);
    if (a > 200)
        c->steer = 100.0f;
    else if (a < -200)
        c->steer = -100.0f;
    else
        c->steer = (float)a * 0.5f;
}

void a2_ai_drive(a2_ai_car *c, const a2_ai_env *e, const float near[3], float factor,
                 float bonus, int cop, float dt)
{
    if (c->d.heading < 0.0f)
        c->d.heading += 4096.0f;
    if (c->d.heading >= 4096.0f)
        c->d.heading -= 4096.0f;
    a2_ai_longitudinal(c, e, factor, bonus, dt);
    a2_ai_steer_to(c, near);
    a2_ai_steering(c, e, cop, dt);
    a2_ai_body(c, e, dt);
}

/* ------------------------------------------------------------ collisions */

/* the integrator's frame: forward (-sin, cos), lateral (cos, sin) */
void a2_ai_world_velocity(const a2_drive *d, float *vx, float *vz)
{
    float h = d->heading * HEAD_TO_RAD, sn = sinf(h), cs = cosf(h);
    *vx = cs * d->v_lat - sn * d->v_fwd;
    *vz = sn * d->v_lat + cs * d->v_fwd;
}

void a2_ai_push(a2_drive *d, float dvx, float dvz, float dw)
{
    float h = d->heading * HEAD_TO_RAD, sn = sinf(h), cs = cosf(h);
    d->v_lat += cs * dvx + sn * dvz;
    d->v_fwd += -sn * dvx + cs * dvz;
    d->yaw_rate += dw;
    if (d->yaw_rate > 6.0f)
        d->yaw_rate = 6.0f;
    if (d->yaw_rate < -6.0f)
        d->yaw_rate = -6.0f;
}

int a2_ai_capsule_push(const a2_drive *d, float half_len, float half_wid, float *x, float *z,
                       float radius, float push[2])
{
    float h = d->heading * HEAD_TO_RAD;
    float fx = -sinf(h), fz = cosf(h);
    float axis = half_len - half_wid;
    if (axis < 0.0f)
        axis = 0.0f;
    float along = (*x - d->x) * fx + (*z - d->z) * fz;
    if (along > axis)
        along = axis;
    if (along < -axis)
        along = -axis;
    float bx = d->x + fx * along, bz = d->z + fz * along;
    float sep = radius + half_wid;
    float dx = *x - bx, dz = *z - bz;
    float d2 = dx * dx + dz * dz;
    if (d2 >= sep * sep)
        return 0;
    float dl = sqrtf(d2);
    if (dl < 1e-4f) {
        dx = 1.0f;
        dz = 0.0f;
        dl = 1.0f;
    }
    *x = bx + dx / dl * sep;
    *z = bz + dz / dl * sep;
    push[0] = dx / dl;
    push[1] = dz / dl;
    return 1;
}
