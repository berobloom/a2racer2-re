// ai_cfun.cpp: tests of the AI state machine (discoveries "The state machine is table-driven").
// The file is named by its asserts' __FILE__. Its line numbers are not known except where
// __LINE__ is used; #line puts those uses where the game has them.
#include <math.h>
#include <stdlib.h>
#include "spel.h"

// The FSM test for cops catching their target: is the target standing (|lateral| + |speed|
// under 7) within sqrt(5000) of the car? Never for the player while the game mode is 3, nor
// for a racer in state 9. The name is ours.
// FUNCTION: SPEL 0x4019f0
int ai_target_caught(int idx)
{
    int target = ai_records[idx].target;
    if (target == player_object) {
        if (game_mode == 3)
            return 0;
    } else {
        int i;
        for (i = 0; i < 3; i++)
            if (ai_records[i].object == target)
                break;
        if (i == 3)
#line 308
            fatal_error("Racer not found! File:%s Line:%d", __FILE__, __LINE__);
        else if (ai_records[i].state == 9)
            return 0;
    }
    float speed = vehicles.data[target].speed;
    float lateral = vehicles.data[target].lateral;
    float moving = fabs(speed) + fabs(lateral);
    if (moving < 7) {
        float dx = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].x
                   - d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].x;
        float dz = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].z
                   - d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].z;
        if (dx * dx + dz * dz < 5000)
            return 1;
    }
    return 0;
}

// The FSM test for 7 -> 0: 4 s have passed; switches the light off. The name is ours.
// FUNCTION: SPEL 0x401b00
int ai_wait_over(int idx)
{
    if (ai_records[idx].timer < 4) {
        ai_records[idx].timer += game_timer.get_dt();
        return 0;
    }
    ai_records[idx].timer = 0;
    siren(ai_records[idx].object, 0);
    return 1;
}

// The FSM test for 0 -> 11 (racers) and the cops' give-up: a racer farther from the player than
// its position's distance (sqrt 2e7, 2e7, 1.75e7, 1.5e7) gives 2 when it is ahead of the
// player, else 3 on the hard difficulty and 1 on the easy one; a cop farther than sqrt(2e7)
// from its target switches its light off and gives 1. The name is ours.
// FUNCTION: SPEL 0x401b60
int ai_too_far(int idx)
{
    float range[4] = {2.0e7f, 2.0e7f, 1.75e7f, 1.5e7f};
    if (idx < 0)
#line 418
        fatal_error("%s (%d) Invalid racer index (%d).", __FILE__, __LINE__, idx);
    if (idx < 3) {
        float dx = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].x
                   - d3d_objects.data[vehicles.data[player_object].d3d].x;
        float dz = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].z
                   - d3d_objects.data[vehicles.data[player_object].d3d].z;
        if (dx * dx + dz * dz > range[racer_slots[idx].position]) {
            if (racer_slots[idx].position < player_position)
                return 2;
            return difficulty ? 3 : 1;
        }
    } else {
        float dx = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].x
                   - d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].x;
        float dz = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].z
                   - d3d_objects.data[vehicles.data[ai_records[idx].target].d3d].z;
        if (dx * dx + dz * dz > 2.0e7f) {
            siren(ai_records[idx].object, 0);
            return 1;
        }
    }
    return 0;
}

// The FSM test for 11 -> 0: within sqrt(1.5e7) = 3873 units of the player. The name is ours.
// FUNCTION: SPEL 0x401cf0
int ai_near_player(int idx)
{
    float dx = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].x
               - d3d_objects.data[vehicles.data[player_object].d3d].x;
    float dz = d3d_objects.data[vehicles.data[ai_records[idx].object].d3d].z
               - d3d_objects.data[vehicles.data[player_object].d3d].z;
    if (dx * dx + dz * dz < 1.5e7f)
        return 1;
    return 0;
}

// The cop FSM test for 4 -> 14 and 16 -> 14: the target no longer faces along the road.
// The name is ours.
// FUNCTION: SPEL 0x401d70
int ai_target_turned(int idx)
{
    return !faces_road(ai_records[idx].target);
}

// The cop FSM test for 14 -> 3: the target faces along the road again, is within 2 chunks and
// drives forward. The name is ours.
// FUNCTION: SPEL 0x401d90
int ai_target_back(int idx)
{
    if (faces_road(ai_records[idx].target)
        && abs(chunk_distance(vehicles.data[ai_records[idx].object].chunk,
                              vehicles.data[ai_records[idx].target].chunk)) < 2
        && vehicles.data[ai_records[idx].target].speed > 0)
        return 1;
    return 0;
}

// The cop FSM test for 3 -> 15, the chase gives up: after 30 s (60 on the hard difficulty) or
// when the target is more than 50 chunks ahead; the light goes off with the first, and the
// radio says the give-up line with both. The name is ours.
// FUNCTION: SPEL 0x401e20
int ai_chase_over(int idx)
{
    float limit = 30;
    if (difficulty)
        limit = 60;
    ai_records[idx].timer += game_timer.get_dt();
    if (ai_records[idx].timer > limit) {
        ai_records[idx].timer = 0;
        siren(ai_records[idx].object, 0);
        radio_give_up(ai_records[idx].target);
        return 1;
    }
    if (chunk_distance(vehicles.data[ai_records[idx].object].chunk,
                       vehicles.data[ai_records[idx].target].chunk) > 50) {
        radio_give_up(ai_records[idx].target);
        return 1;
    }
    return 0;
}

// The cop FSM test for 17 -> 8 and 15 -> 8: at 5 or slower the car stops dead. The name is
// ours.
// FUNCTION: SPEL 0x401ef0
int ai_stopped(int idx)
{
    if (vehicles.data[ai_records[idx].object].speed <= 5) {
        vehicles.data[ai_records[idx].object].speed = 0;
        vehicles.data[ai_records[idx].object].lateral = 0;
        vehicles.data[ai_records[idx].object].yaw_rate = 0;
        return 1;
    }
    return 0;
}

// The cop FSM test for 3 -> 16 and 4 -> 16: the target drives against its waypoint's lane and
// faces along the road. The name is ours.
// FUNCTION: SPEL 0x401f60
int ai_target_wrong_way(int idx)
{
    if (waypoint_lane(ai_records[idx].target, vehicles.data[ai_records[idx].target].waypoint) == -1
        && faces_road(ai_records[idx].target))
        return 1;
    return 0;
}

// The cop FSM test for 3, 4, 16 -> 14: the target reverses. The name is ours.
// FUNCTION: SPEL 0x401fb0
int ai_target_reversing(int idx)
{
    if (vehicles.data[ai_records[idx].target].speed < 0)
        return 1;
    return 0;
}

// The cop FSM test for 16 -> 10: 2 s in one chunk while the target does more than 10. Under
// 2 s the chunk timer is reset. The name is ours.
// FUNCTION: SPEL 0x401ff0
int ai_stuck_in_chase(int idx)
{
    if (ai_records[idx].chunk_time > 2) {
        if (vehicles.data[ai_records[idx].target].speed > 10)
            return 1;
        return 0;
    }
    ai_records[idx].chunk_time = 0;
    return 0;
}

// The FSM test for 0 -> 10 and 3 -> 10: 3 s in the same chunk (stuck). The name is ours.
// FUNCTION: SPEL 0x402050
int ai_stuck(int idx)
{
    if (ai_records[idx].chunk_time > 3)
        return 1;
    return 0;
}

// The cop FSM test for 16 -> 3: the target faces along the road and drives with its
// waypoint's lane again. The name is ours.
// FUNCTION: SPEL 0x402080
int ai_target_right_way(int idx)
{
    int target = ai_records[idx].target;
    int waypoint = vehicles.data[target].waypoint;
    if (faces_road(target) && waypoint_lane(target, waypoint) == 1)
        return 1;
    return 0;
}
