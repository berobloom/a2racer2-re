#include "handling.h"

#include "a2_io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VA_TABLE 0x484B18
#define STRIDE 238
#define NAME_LEN 14

/* Map a virtual address to a file offset through the PE section table. */
static long va_to_off(const uint8_t *d, size_t n, uint32_t va)
{
    if (n < 0x40)
        return -1;
    uint32_t pe = (uint32_t)(d[0x3c] | (d[0x3d] << 8) | (d[0x3e] << 16) | ((uint32_t)d[0x3f] << 24));
    if (pe + 24 > n || memcmp(d + pe, "PE\0\0", 4) != 0)
        return -1;
    int nsec = d[pe + 6] | (d[pe + 7] << 8);
    int opt = d[pe + 20] | (d[pe + 21] << 8);
    uint32_t base;
    memcpy(&base, d + pe + 0x34, 4);
    for (int i = 0; i < nsec; i++) {
        const uint8_t *e = d + pe + 24 + (size_t)opt + (size_t)i * 40;
        uint32_t vaddr, rsize, raw;
        memcpy(&vaddr, e + 12, 4);
        memcpy(&rsize, e + 16, 4);
        memcpy(&raw, e + 20, 4);
        uint32_t lo = base + vaddr;
        if (va >= lo && va < lo + rsize) {
            long off = (long)raw + (long)(va - lo);
            return (off >= 0 && (size_t)off < n) ? off : -1;
        }
    }
    return -1;
}

static float fld(const uint8_t *p, int k)
{
    float f;
    memcpy(&f, p + NAME_LEN + (size_t)k * 4, 4);
    return f;
}

int a2_handling_load(a2_handling_table *t, const char *datadir)
{
    memset(t, 0, sizeof(*t));
    char path[1024];
    snprintf(path, sizeof path, "%s/spel.dat", datadir);
    size_t size = 0;
    uint8_t *image = a2_read_file(path, &size);
    if (!image) {
        fprintf(stderr, "handling: cannot read %s\n", path);
        return -1;
    }
    long off = va_to_off(image, size, VA_TABLE);
    if (off < 0 || (size_t)off + (size_t)STRIDE * A2_HANDLING_COUNT > size) {
        fprintf(stderr, "handling: table not in a mapped section\n");
        free(image);
        return -1;
    }

    for (int i = 0; i < A2_HANDLING_COUNT; i++) {
        const uint8_t *p = image + off + (size_t)i * STRIDE;
        a2_handling *h = &t->v[i];
        memcpy(h->name, p, NAME_LEN);
        h->name[NAME_LEN] = '\0';
        h->mass = fld(p, 2);
        h->length = fld(p, 3);
        h->width = fld(p, 4);
        h->cog_to_front = fld(p, 5);
        h->cog_to_rear = fld(p, 6);
        h->wheelbase = h->cog_to_front + h->cog_to_rear;
        h->frontal_area = fld(p, 9);
        h->f35 = fld(p, 35);
        h->f36 = fld(p, 36);
        h->f47 = fld(p, 47);
        h->f48 = fld(p, 48);
        h->grip_front = fld(p, 17);
        h->grip_rear = fld(p, 20);
        h->grip_mult = fld(p, 54);
        h->brake_coef = fld(p, 53);
        h->traction_cap = fld(p, 52);
        h->drive_front = fld(p, 38);
        /* Field 55 is an integer in a table of floats; read the four bytes as
         * one rather than through fld(). */
        memcpy(&h->engine_sound, p + 14 + 4 * 55, 4);
        h->zero_to_100 = fld(p, 10);
        h->gravity = fld(p, 12);
        h->air_density = fld(p, 13);
        h->rolling = fld(p, 21);
        /* Field 22 is the wheel radius, not Cd (see a2_handling_wheel_radius).
         * The drag coefficient is field 8, which the earlier reading had as the
         * centre-of-gravity height: it is the field the drag cluster loads
         * beside the air density and the frontal area, and its values are
         * better Cds than radii -- 0.38 for an ordinary car, 0.33 for the
         * Diablo, 0.65 for the bus. That last one is what decides it: a bus's
         * Cd really is about 0.65, while its centre of gravity is nowhere near
         * 0.65 m off the ground. Marked LIKELY rather than certain -- the
         * multiply has not been traced end to end. */
        h->wheel_radius_table = fld(p, 22); /* the real radius, recorded not used */
        h->cd_table = fld(p, 8);            /* the real Cd, recorded not used */
        /* The model is fitted around field 22 standing in for Cd and a derived
         * radius. That pairing is physically wrong but numerically well
         * behaved; see a2_handling_wheel_radius. */
        h->drag_cd = fld(p, 22);
        h->rpm_max = fld(p, 23);
        h->power = fld(p, 24);
        h->rpm_torque = fld(p, 25);
        for (int g = 0; g < A2_HANDLING_GEARS; g++) {
            h->gear[g] = fld(p, 28 + g);
            h->gear_up[g] = fld(p, 40 + g);
        }
        h->final_drive = fld(p, 34);
        h->final_drive_up = fld(p, 46);
        h->valid = h->mass > 1.0f && h->rpm_max > 1.0f && h->final_drive > 0.1f;
    }
    t->count = A2_HANDLING_COUNT;

    /* Sanity: the table should look like the one we reverse engineered. */
    if (strcmp(t->v[0].name, "RACER_1_ANIM") != 0 || t->v[0].gravity < 9.0f ||
        t->v[0].gravity > 10.5f) {
        fprintf(stderr, "handling: %s does not match the expected build; ignoring the table\n",
                path);
        free(image);
        memset(t, 0, sizeof(*t));
        return -1;
    }
    fprintf(stderr, "handling: %d vehicles (g=%.2f, rho=%.2f)\n", t->count, t->v[0].gravity,
            t->v[0].air_density);
    free(image);
    return 0;
}

int a2_handling_find(const a2_handling_table *t, const char *anim_name)
{
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->v[i].name, anim_name) == 0)
            return i;
    return -1;
}

int a2_handling_top_gear(const a2_handling *h, int upgraded)
{
    const float *g = upgraded ? h->gear_up : h->gear;
    int top = 0;
    for (int i = 0; i < A2_HANDLING_GEARS; i++)
        if (g[i] > 0.0f)
            top = i;
    return top;
}

float a2_handling_wheel_radius(const a2_handling *h, int upgraded, float top_speed_ms)
{
    const float *g = upgraded ? h->gear_up : h->gear;
    float fd = upgraded && h->final_drive_up > 0.1f ? h->final_drive_up : h->final_drive;
    int top = a2_handling_top_gear(h, upgraded);
    /* The table gives the wheel radius outright in field 22, and the game's own
     * driveline formula proves it: at 0x40e299 it computes
     * `gear * final * 9.554 / field22`, and 9.554 is 60/(2*pi) -- the constant
     * that turns road speed into engine rpm, with the radius as the divisor.
     * The values are the radii they look like: 0.29-0.33 m for a car, 0.50 for
     * the bus. Feeding the rated top speeds back through gives 4350-6610 rpm
     * against a 9000 redline, which is what a car geared to be drag-limited
     * should do.
     *
     * This replaces deriving it from the assumption that a car sits exactly on
     * the redline at its rated top speed -- which those rpm figures show it
     * does not.
     *
     * **The model does not use it**, and here is why: feeding
     * the table's radius and Cd in makes the *inputs* right and the *behaviour*
     * worse, because the torque curve is still synthesised. With the real
     * radius the solver hands the engine far more torque, and a Trabant that
     * should take 13 s to 100 km/h does it in 5.9. Adding a traction limit off
     * the grip and CoG fields recovers the Trabant (9.9 s) but leaves the quick
     * cars three times too fast, because for them the cap never binds. Until
     * the real torque curve is identified -- fields 35/36 are the best
     * candidate -- the fitted radius below reproduces the game's own 0-100
     * figures far better, so that is what is kept. */
    if (g[top] <= 0.0f || h->rpm_max <= 0.0f || top_speed_ms <= 0.0f)
        return 0.3f;
    float r = top_speed_ms * g[top] * fd * 60.0f / (h->rpm_max * 2.0f * (float)M_PI);
    if (r < 0.15f)
        r = 0.15f;
    if (r > 0.8f)
        r = 0.8f;
    return r;
}

float a2_handling_drive_force(const a2_handling *h, int upgraded, int gear, float v,
                              float wheel_radius, float *out_rpm)
{
    const float *g = upgraded ? h->gear_up : h->gear;
    float fd = upgraded && h->final_drive_up > 0.1f ? h->final_drive_up : h->final_drive;
    if (gear < 0 || gear >= A2_HANDLING_GEARS || g[gear] <= 0.0f || wheel_radius <= 0.0f) {
        if (out_rpm)
            *out_rpm = 0.0f;
        return 0.0f;
    }
    float ratio = g[gear] * fd;
    /* Engine speed from road speed through the driveline. */
    float rpm = v * ratio * 60.0f / (2.0f * (float)M_PI * wheel_radius);
    float idle = h->rpm_torque * 0.25f;
    if (rpm < idle)
        rpm = idle;
    if (rpm > h->rpm_max)
        rpm = h->rpm_max;
    if (out_rpm)
        *out_rpm = rpm;

    /* Torque curve: peak at rpm_torque, tapering either side.  The table gives
     * the peak-torque rpm and the redline but no curve, so the shape is ours --
     * a simple parabola through the peak, floored so the engine never makes
     * nothing. */
    float x = (rpm - h->rpm_torque) / (h->rpm_max - h->rpm_torque > 1.0f
                                           ? h->rpm_max - h->rpm_torque
                                           : 1.0f);
    float k = h->torque_falloff > 0.0f ? h->torque_falloff : 0.45f;
    float shape = 1.0f - k * x * x;
    if (shape < 0.15f)
        shape = 0.15f;

    /* Peak torque from the rated power at the torque peak: P = T * omega.
     * `power` is taken as metric horsepower, which is what makes the fleet read
     * correctly (Trabant 45, Diablo 580). */
    float watts = h->power * 735.5f;
    float omega = h->rpm_torque * 2.0f * (float)M_PI / 60.0f;
    float peak_torque = omega > 1.0f ? watts / omega : 0.0f;

    return peak_torque * shape * ratio / wheel_radius;
}

float a2_handling_resist_force(const a2_handling *h, float v)
{
    /* The area the model is fitted around is the old body-box estimate, not the
     * table's own field 9 (the real frontal area, recorded as `frontal_area`):
     * see a2_handling_wheel_radius for why the fitted pairing is kept. */
    float area = h->width * h->cog_to_front;
    float drag = 0.5f * h->air_density * h->drag_cd * area * v * v;
    float roll = h->rolling * h->mass * h->gravity;
    return drag + (v > 0.1f ? roll : 0.0f);
}

/* Simulate a standing start and return the time to 100 km/h, or -1. */
static float zero_to_100_sim(const a2_handling *h, int upgraded, float scale)
{
    float v = 0.0f, dt = 0.002f, t = 0.0f;
    for (int s = 0; s < 120000; s++) {
        int gear = 0;
        float rpm = 0.0f;
        for (int g = 0; g < A2_HANDLING_GEARS; g++) {
            if (a2_handling_drive_force(h, upgraded, g, v, h->wheel_radius, &rpm) <= 0.0f)
                continue;
            gear = g;
            if (rpm < h->rpm_max * 0.98f)
                break;
        }
        float F = a2_handling_drive_force(h, upgraded, gear, v, h->wheel_radius, &rpm) * scale;
        if (rpm >= h->rpm_max * 0.999f)
            F = 0.0f;
        float a = (F - a2_handling_resist_force(h, v)) / h->mass;
        v += a * dt;
        if (v < 0.0f)
            v = 0.0f;
        t += dt;
        if (v >= 27.78f)
            return t;
    }
    return -1.0f;
}

/* Solve the curve shape against the stock configuration, where both published
 * figures belong together: CARS.DAT's stock top speed and this table's field 10.
 * `zero_to_100` is a stock figure, so pairing it with an upgraded top speed
 * would flatten the curve to hit a time the upgraded car should beat. */
static void solve_shape(a2_handling *h, float stock_top_ms)
{
    int top = a2_handling_top_gear(h, 0);
    float r = a2_handling_resist_force(h, stock_top_ms);
    float lo = 0.0f, hi = 0.95f;
    for (int i = 0; i < 30; i++) {
        float k = 0.5f * (lo + hi);
        h->torque_falloff = k;
        float rpm = 0.0f;
        float f = a2_handling_drive_force(h, 0, top, stock_top_ms, h->wheel_radius, &rpm);
        h->torque_scale = (f > 1.0f && r > 1.0f) ? r / f : 1.0f;
        if (h->zero_to_100 <= 0.5f)
            break;
        float t = zero_to_100_sim(h, 0, h->torque_scale);
        /* A peakier curve (larger k) leaves less torque at the redline, so the
         * scale that balances drag at top speed comes out larger, which lifts
         * torque everywhere and makes the standing start *quicker*.  So: too
         * slow means search upward. */
        if (t < 0.0f || t > h->zero_to_100)
            lo = k;
        else
            hi = k;
    }
}

void a2_handling_calibrate(a2_handling *h, int upgraded, float top_speed_ms)
{
    h->wheel_radius = a2_handling_wheel_radius(h, upgraded, top_speed_ms);
    h->torque_scale = 1.0f;
    h->torque_falloff = 0.45f;
    if (!h->valid || top_speed_ms <= 1.0f)
        return;

    /* Solve the scale so the car just reaches its rated top speed: there, the
     * tractive force in top gear equals the drag plus rolling resistance.
     *
     * The alternative was to solve it against the table's own 0-100 time
     * (field 10), which it then matched exactly for all seven cars -- but that
     * left every car drag-limited well short of its rated speed (a Trabant
     * stopped at 136 km/h against 151), because the synthesised torque curve
     * gives too much at the peak and too little at the top end.  The rated top
     * speed is the figure the game advertises and the one a player notices, so
     * that is what this pins; `zero_to_100` is reported as a diagnostic
     * instead.  Both cannot hold at once without the real torque curve, which
     * the table does not carry. */
    /* Shape from the stock pairing, then the scale for whatever configuration is
     * actually being driven, so the upgraded car keeps the car's character and
     * simply goes faster. */
    float stock_r = a2_handling_wheel_radius(h, 0, h->stock_top_ms > 1.0f ? h->stock_top_ms
                                                                         : top_speed_ms);
    float saved_r = h->wheel_radius;
    h->wheel_radius = stock_r;
    solve_shape(h, h->stock_top_ms > 1.0f ? h->stock_top_ms : top_speed_ms);
    h->wheel_radius = saved_r;

    int top = a2_handling_top_gear(h, upgraded);
    float r = a2_handling_resist_force(h, top_speed_ms);
    float rpm = 0.0f;
    float f = a2_handling_drive_force(h, upgraded, top, top_speed_ms, h->wheel_radius, &rpm);
    if (f > 1.0f && r > 1.0f)
        h->torque_scale = r / f;
    if (h->torque_scale < 0.05f)
        h->torque_scale = 0.05f;
    if (h->torque_scale > 20.0f)
        h->torque_scale = 20.0f;
}

/* Time to 100 km/h under the calibrated model -- a diagnostic, to compare with
 * the table's own field 10. */
float a2_handling_zero_to_100(const a2_handling *h, int upgraded)
{
    return zero_to_100_sim(h, upgraded, h->torque_scale);
}

float a2_handling_lateral_limit(const a2_handling *h)
{
    /* The game works the two axles separately; the port has one body, so take
     * the mean of the front and rear peak factors. */
    float grip = 0.5f * (h->grip_front + h->grip_rear);
    float mult = h->grip_mult > 0.0f ? h->grip_mult : 1.0f;
    float g = h->gravity > 0.0f ? h->gravity : 9.81f;
    if (grip <= 0.0f)
        return A2_GRIP_REFERENCE;
    return grip * mult * g;
}

float a2_handling_brake_limit(const a2_handling *h)
{
    float g = h->gravity > 0.0f ? h->gravity : 9.81f;
    if (h->brake_coef <= 0.0f)
        return 8.0f;
    return h->brake_coef * g;
}

void a2_handling_apply_upgrades(a2_handling *h, int motor, int banden)
{
    /* The multipliers are the doubles at 0x47f880 (1.3), 0x47f888 (1.1) and
     * 0x47f890 (1.2).  Run through the stock-calibrated model, motor alone
     * lands each car within 0-3% of CARS.DAT's *upgraded* top speed (Trabant
     * 165.6 against 165, Diablo 262.8 against 255), which is why the port no
     * longer switches to that CARS.DAT figure itself. */
    if (motor) {
        h->power *= 1.3f;
        h->rpm_torque *= 1.1f;
        h->rpm_max *= 1.1f;
    }
    if (banden) {
        h->grip_front *= 1.2f;
        h->grip_rear *= 1.2f;
    }
}

void a2_handling_axle_shares(const a2_handling *h, float *front, float *rear)
{
    float wb = h->cog_to_front + h->cog_to_rear;
    float f = wb > 0.01f ? h->cog_to_rear / wb : 0.5f;
    *front = f;
    *rear = 1.0f - f;
}
