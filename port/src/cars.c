#include "cars.h"

#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_A 124
#define BLOCK_B 24

int a2_cars_load(a2_cars *c, const char *datadir)
{
    memset(c, 0, sizeof(*c));
    char path[1024];
    snprintf(path, sizeof path, "%s/CARS.DAT", datadir);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    if (!data) {
        fprintf(stderr, "cars: cannot read %s\n", path);
        return -1;
    }
    size_t want = (size_t)A2_CAR_SLOTS * (BLOCK_A + BLOCK_B);
    if (size != want) {
        fprintf(stderr, "cars: %s is %zu bytes, expected %zu\n", path, size, want);
        free(data);
        return -1;
    }

    a2_cur cur;
    for (int i = 0; i < A2_CAR_SLOTS; i++) {
        a2_cur_init(&cur, data + (size_t)i * BLOCK_A, BLOCK_A);
        a2_car *car = &c->cars[i];
        for (int k = 0; k < A2_CAR_GEARS; k++)
            car->accel[k] = a2_f32(&cur);
        for (int k = 0; k < A2_CAR_GEARS; k++)
            car->accel_upgraded[k] = a2_f32(&cur);
        for (int k = 0; k < A2_CAR_GEARS; k++)
            car->max_speed[k] = a2_f32(&cur);
        for (int k = 0; k < A2_CAR_GEARS; k++)
            car->max_speed_upgraded[k] = a2_f32(&cur);
        car->upshift_point = a2_f32(&cur);
        car->downshift_point = a2_f32(&cur);
        car->unused_26 = a2_f32(&cur);
        car->brake_force = a2_f32(&cur);
        car->engine_decel = a2_f32(&cur);
        car->roll_rate = a2_f32(&cur);
        car->steer_speed = a2_f32(&cur);

        a2_cur_init(&cur, data + (size_t)A2_CAR_SLOTS * BLOCK_A + (size_t)i * BLOCK_B, BLOCK_B);
        car->roll_damping = a2_f32(&cur);
        car->roll_factor = a2_f32(&cur);
        car->pitch_damping = a2_f32(&cur);
        car->pitch_factor = a2_f32(&cur);
        car->height_damping = a2_f32(&cur);
        car->height_factor = a2_f32(&cur);

        car->empty = 1;
        for (int k = 0; k < A2_CAR_GEARS; k++)
            if (car->max_speed[k] != 0.0f)
                car->empty = 0;
    }
    free(data);
    return 0;
}

float a2_car_top_speed(const a2_car *car, int upgraded)
{
    const float *a = upgraded ? car->max_speed_upgraded : car->max_speed;
    float top = 0.0f;
    for (int k = 0; k < A2_CAR_GEARS; k++)
        if (a[k] > top)
            top = a[k];
    return top;
}

int a2_car_gear_for_speed(const a2_car *car, float speed, int upgraded)
{
    const float *a = upgraded ? car->max_speed_upgraded : car->max_speed;
    int top = 0;
    for (int k = 0; k < A2_CAR_GEARS; k++) {
        if (a[k] <= 0.0f)
            continue;
        top = k;
        if (speed < a[k])
            return k;
    }
    return top;
}

const char *a2_car_anim_name(int slot)
{
    static const char *const names[7] = {"RACER_1_ANIM", "RACER_2_ANIM", "RACER_3_ANIM",
                                         "RACER_4_ANIM", "RACER_5_ANIM", "RACER_6_ANIM",
                                         "RACER_7_ANIM"};
    return (slot >= 0 && slot < 7) ? names[slot] : names[0];
}

/* Half-extents of each slot's mesh, X (across) and Z (along), in world units.
 * Measured from the shipped `.d3d` bounding boxes:
 *
 *   0 r_trabbi 16.4x39.2   1 r_astra 16.9x42.6   2 r_beetle 17.3x36.5
 *   3 r_bmw    17.1x39.2   4 r_porche 17.4x43.4  5 r_clk    17.9x46.0
 *   6 r_diablo 18.8x48.0
 *
 * CARS.DAT itself has no dimensions -- it is a performance table -- so these
 * come from the geometry rather than from the data file.  Slots 7-9 are empty
 * and fall back to the Trabant's.
 */
static const struct {
    float half_wid, half_len;
} CAR_SIZE[A2_CAR_SLOTS] = {
    {8.2f, 19.6f}, {8.5f, 21.3f}, {8.7f, 18.2f}, {8.5f, 19.6f}, {8.7f, 21.7f},
    {9.0f, 23.0f}, {9.4f, 24.0f}, {8.2f, 19.6f}, {8.2f, 19.6f}, {8.2f, 19.6f},
};

float a2_car_half_width(int slot)
{
    if (slot < 0 || slot >= A2_CAR_SLOTS)
        slot = 0;
    return CAR_SIZE[slot].half_wid;
}

float a2_car_half_length(int slot)
{
    if (slot < 0 || slot >= A2_CAR_SLOTS)
        slot = 0;
    return CAR_SIZE[slot].half_len;
}
