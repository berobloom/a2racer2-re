// unplaced.cpp: functions of spel.dat whose source file is not known yet.
// reverse-engineering/analysis/spel_modules.tsv says which file each function is from when a
// __FILE__ string or its neighbours tell; these are code before ai_cfun.cpp, so from a file
// that sorts before it (the objects are linked in alphabetical order).
#include <stdlib.h>
#include <math.h>
#include "spel.h"

// The roadside assistance (ADAC) car's AI, named by its first trace line, "Handling Adac
// AI." It drives toward the player as traffic until it is within 3 chunks, brakes there and
// waits; once it has stopped (adac_state 1) it keeps up with the player at 0.9 of its speed,
// no faster than its behaviour's preferred speed. Returns 1, or what HANDLE_TRAFFIC_AI returns.
// FUNCTION: SPEL 0x401000
int handle_adac_ai(int object)
{
    trace("Handling Adac AI.\n");
    if (race_running && flag_4d9a78) {
        int ai = vehicles.data[object].ai;
        fn_4084b0(object);
        if (ai_instances.data[ai].f2c == 0) {
            trace("No collision\n");
            if (adac_state == 1) {
                trace("Adac has arrived.\n");
                // The compares and the copy go through doubles: the game's code is the code of
                // these spellings (fld/fcomp st(1), fcompp, fld/fstp), not of plain float ones.
                // What the original wrote is not known, only that it computed in double.
                double lim = vehicles.data[player_object].speed * 0.9f;
                if (ai_instances.data[ai].max_speed < lim) {
                    ai_instances.data[ai].max_speed = (float)lim;
                    if ((double)ai_instances.data[ai].max_speed
                            > ai_behaviours.data[ai_instances.data[ai].behaviour].preferred_speed)
                        ai_instances.data[ai].max_speed = (float)(1.0 *
                            ai_behaviours.data[ai_instances.data[ai].behaviour].preferred_speed);
                }
                return handle_traffic_ai(object);
            }
            int adac_chunk = vehicles.data[object].chunk;
            int player_chunk = vehicles.data[player_object].chunk;
            int diff = adac_chunk - player_chunk;
            if (diff < 0)
                diff = -diff;
            trace("Adac chunk = %d. Player chunk = %d.\nDifference = %d. AI_ADAC_RANGE = %d\n",
                  adac_chunk, player_chunk, diff, 3);
            if (diff < 3) {
                trace("In range, braking. Speed = %f.\n", vehicles.data[object].speed);
                ai_instances.data[ai].max_speed = 0;
                if (vehicles.data[object].speed == 0)
                    adac_state = 1;
            }
            trace("\tCalling handle_basic_ai.\n");
            handle_basic_ai(object);
        }
        fn_40c4a0();
        trace("\tCalling sndfx_play_object_sample.\n");
        sndfx_play_object_sample(object, ai_instances.data[ai].sample_loop);
        C3dObject *o = &d3d_objects.data[vehicles.data[object].d3d];
        o->y = fn_40ede0(object);
    }
    return 1;
}

// FUNCTION: SPEL 0x401200
void AIControl::reset()
{
    drift_timer = 0;
    drift_sum = 0;
    drift_start = 0;
    f24 = 0;
    stopped = 0;
    steer = 0;
    throttle = 0;
    brake = 0;
    gear = 0;
    coast = 0;
}

// The assert names it: "Out of FSM array index. Function: CalculateStearing".
// An index of 15 or more reports the error and goes on with an uninitialised object, as the
// original does (the compiler reads the argument's stack slot).
// FUNCTION: SPEL 0x401230
void CalculateStearing(int idx, const float *point)
{
    int object;
    if (idx < 15)
        object = ai_records[idx].object;
    else
        fatal_error("Out of FSM array index. Function: CalculateStearing");
    int angle = rel_angle(object, point);
    ai_records[idx].control.steer = angle > 200 ? 100.0f : angle < -200 ? -100.0f : angle * 0.5f;
}

// Throttle and brake toward a target speed of 70 - |angle| / 15 (an angle over 1000 counts as
// 500): below it the throttle opens at 75 per second; above it a racer (records 0-2) brakes
// at 400 per second, anyone else takes the target speed at once. The name is ours
// (port/src/ai_car.h a2_ai_speed_control).
// FUNCTION: SPEL 0x4012c0
void ai_speed_control(int idx, const float *point)
{
    int angle = abs(rel_angle(ai_records[idx].object, point));
    if (angle > 1000)
        angle = 500;
    int target = 70 - angle / 15;
    float *speed = &vehicles.data[ai_records[idx].object].speed;
    if (target > *speed) {
        ai_records[idx].control.throttle -= game_timer.get_dt() * -75.0f;
        if (ai_records[idx].control.throttle > 100.0f)
            ai_records[idx].control.throttle = 100.0f;
        ai_records[idx].control.brake = 0;
    } else if (idx < 3) {
        ai_records[idx].control.brake -= game_timer.get_dt() * -400.0f;
        if (ai_records[idx].control.brake > 100.0f)
            ai_records[idx].control.brake = 100.0f;
    } else {
        *speed = (float)target;
    }
}

// One drive step of a computer car: wrap its heading into 0..4096, then the gear (LIKELY,
// discoveries: 0x40ff90 writes +0x44), the steering toward the point, and the rest of the car.
// The name is ours (port/src/ai_car.h a2_ai_drive).
// FUNCTION: SPEL 0x4013c0
void ai_drive(int idx, const float *point)
{
    int object = ai_records[idx].object;
    if (d3d_objects.data[vehicles.data[object].d3d].yaw < 0)
        d3d_objects.data[vehicles.data[object].d3d].yaw -= -4096.0f;
    if (d3d_objects.data[vehicles.data[object].d3d].yaw >= 4096.0f)
        d3d_objects.data[vehicles.data[object].d3d].yaw -= 4096.0f;
    fn_40ff90(idx);
    CalculateStearing(idx, point);
    fn_410280(idx);
    fn_40ebd0(object);
    fn_40ece0(object);
    fn_40ee80(object);
}

// The rubber band: racer idx's speed factor from its lead over the player in chunks
// (clamped to +-50). Past the stage's limit the factor is 1 - lead * gain / 50; inside it a
// fixed factor by racer and difficulty. Kept within 0.2..2. The name is ours
// (port/src/ai_car.c a2_ai_rubber_band).
// FUNCTION: SPEL 0x4014e0
float ai_rubber_band(int idx)
{
    float factor[4][3] = {
        {0.95f, 0.92f, 0.90f},
        {1.02f, 1.01f, 1.00f},
        {1.00f, 0.98f, 0.95f},
        {1.15f, 1.10f, 1.05f},
    };
    float f;
    if (idx < 3) {
        float lead = (float)(ai_instances.data[vehicles.data[racer_slots[idx].object].ai].progress
                             - player_progress);
        if (lead > 0) {
            if (lead > 50)
                lead = 50;
            if (lead > rubber_band[difficulty][etappe][idx].upper)
                f = 1 - lead * rubber_band[difficulty][etappe][idx].gain * 0.02f;
            else
                f = difficulty == 0 ? factor[0][idx] : factor[1][idx];
        } else {
            if (lead < -50)
                lead = -50;
            if (lead < rubber_band[difficulty][etappe][idx].lower)
                f = 1 - lead * rubber_band[difficulty][etappe][idx].gain * 0.02f;
            else
                f = difficulty == 0 ? factor[2][idx] : factor[3][idx];
        }
    } else {
        f = 1.0f;
    }
    if (f < 0.2f)
        f = 0.2f;
    if (f > 2.0f)
        f = 2.0f;
    return f;
}

// The bearing of a point from an object, 0..4096 (0 = dead ahead, 2048 = behind). The x
// distance is kept 0.1 away from 0 so the division cannot blow up. The name is ours
// (port/src/ai_car.c a2_ai_bearing).
// FUNCTION: SPEL 0x401690
int bearing(int object, const float *point)
{
    float dx = point[0] - d3d_objects.data[vehicles.data[object].d3d].x;
    float dz = point[2] - d3d_objects.data[vehicles.data[object].d3d].z;
    C3dObject *o = &d3d_objects.data[vehicles.data[object].d3d];
    if (dx > 0 && dx < 0.1f)
        dx = 0.1f;
    if (dx < 0 && dx > -0.1f)
        dx = -0.1f;
    float a = (float)atan(dz / dx) * 0.15915494f;
    a *= 4096;
    if (dx > 0)
        a -= -3072;
    else
        a -= -1024;
    a -= o->yaw;
    if (a > 4096)
        a -= 4096;
    if (a < 0)
        a -= -4096;
    return (int)a;
}

// The FSM test for 0 -> 1 (discoveries "The state machine is table-driven"): once a second,
// is the player within 100 units and behind the racer (bearing 1500..2500), with the racer
// leading the race? It also makes the player the racer's target. The name is ours.
// FUNCTION: SPEL 0x401780
int ai_player_behind(int idx)
{
    if (ai_records[idx].timer > 1.0f) {
        ai_records[idx].timer = 0;
        return 0;
    }
    int object = ai_records[idx].object;
    float p[3];
    p[0] = d3d_objects.data[vehicles.data[player_object].d3d].x;
    p[1] = d3d_objects.data[vehicles.data[player_object].d3d].y;
    p[2] = d3d_objects.data[vehicles.data[player_object].d3d].z;
    float dx = d3d_objects.data[vehicles.data[object].d3d].x - p[0];
    float dz = d3d_objects.data[vehicles.data[object].d3d].z - p[2];
    if (dx * dx + dz * dz < 10000) {
        int b = bearing(object, p);
        if (b > 1500 && b < 2500) {
            ai_records[idx].target = player_object;
            if (racer_slots[idx].position == 0)
                return 1;
        }
    }
    return 0;
}

// The FSM test for 1 -> 2: the racer leads, goes at least 30, and with a chance of
// 50 * dt per cent while its timer is under 1 s. Otherwise the timer restarts. The name is ours.
// FUNCTION: SPEL 0x4018b0
int ai_lead_chance(int idx)
{
    if (racer_slots[idx].position == 0 && vehicles.data[ai_records[idx].object].speed >= 30) {
        float chance = game_timer.get_dt() * 50;
        if (rand() % 100 < chance && ai_records[idx].timer < 1.0f)
            return 1;
        ai_records[idx].timer = 0;
    }
    return 0;
}

// Is the record's target behind it (bearing 1700..2300 of 4096)? The name is ours.
// FUNCTION: SPEL 0x401960
int ai_target_behind(int idx)
{
    float p[3];
    p[0] = d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].x;
    p[2] = d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].z;
    int b = bearing(ai_records[idx].object, p);
    if (b > 1700 && b < 2300)
        return 1;
    return 0;
}
