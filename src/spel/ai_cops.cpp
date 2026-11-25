// ai_cops.cpp: the police AI (discoveries "the active cops", "RESET_COP, BACKTRACK_SPEED_TRIGGER
// and cop_lane"). The file is named by its asserts' __FILE__.
#include <stdlib.h>
#include "spel.h"

// GLOBAL: SPEL 0x4a5238
T_ARRAY<t_cop> cop_states("Cop States", "t_cop");

// The file's dynamic initialiser: constructs cop_states and registers $E2 with atexit.
// SYNTHETIC: SPEL 0x4020d0
// $E4

// cop_states' destructor, run at exit.
// SYNTHETIC: SPEL 0x4021a0
// $E2

// Sets the cop's speed and lane limits from the last signs behind its car: speed triggers
// 4, 5, 6 are 60, 80, 120 km/h and 8 twice the player's top speed; lane triggers 18..21 on
// carriageway 1 and 40..43 on carriageway 0 give 1..4 lanes.
// FUNCTION: SPEL 0x402200
void RESET_COP(int object, int cop)
{
    int lane;
    int speed;
    trace("\nRESET_COP()\n");
    backtrack_speed_trigger(object, &speed, &lane);
    trace("Speed trigger value = %d.\n", speed);
    switch (speed) {
    case 8:
        trace("Setting max speed to %f.\n", player_top_speed * 2);
        cop_states.data[cop].max_speed = player_top_speed * 2;
        break;
    case 4:
        trace("Setting max speed to %f.\n", 16.67f);
        cop_states.data[cop].max_speed = 16.67f;
        break;
    case 5:
        trace("Setting max speed to %f.\n", 22.22f);
        cop_states.data[cop].max_speed = 22.22f;
        break;
    case 6:
        trace("Setting max speed to %f.\n", 33.33f);
        cop_states.data[cop].max_speed = 33.33f;
        break;
    default:
        trace("ILLEGAL SPEED TRIGGER!!!!\n");
        break;
    }
    if (vehicles.data[object].side == 1) {
        trace("Lane trigger value = %d.\n", lane);
        switch (lane) {
        case 18:
            trace("Setting max lanes = %d.\n", 1);
            cop_states.data[cop].max_lanes = 1;
            break;
        case 19:
            trace("Setting max lanes = %d.\n", 2);
            cop_states.data[cop].max_lanes = 2;
            break;
        case 20:
            trace("Setting max lanes = %d.\n", 3);
            cop_states.data[cop].max_lanes = 3;
            break;
        case 21:
            trace("Setting max lanes = %d.\n", 4);
            cop_states.data[cop].max_lanes = 4;
            break;
        default:
            trace("ILLEGAL LANE TRIGGER!!!\n");
            break;
        }
    }
    if (vehicles.data[object].side == 0) {
        trace("Lane trigger value = %d.\n", lane);
        switch (lane) {
        case 40:
            trace("Setting max lanes = %d.\n", 1);
            cop_states.data[cop].max_lanes = 1;
            break;
        case 41:
            trace("Setting max lanes = %d.\n", 2);
            cop_states.data[cop].max_lanes = 2;
            break;
        case 42:
            trace("Setting max lanes = %d.\n", 3);
            cop_states.data[cop].max_lanes = 3;
            break;
        case 43:
            trace("Setting max lanes = %d.\n", 4);
            cop_states.data[cop].max_lanes = 4;
            break;
        default:
            trace("ILLEGAL LANE TRIGGER!!!\n");
            break;
        }
    }
}

// Which carriageway (0 or 1) of the chunk the waypoint is on, and its place there.
// The name is ours.
// FUNCTION: SPEL 0x402520
void waypoint_side(int chunk, int waypoint, int *side, int *index)
{
    int i;
    for (i = 0; i < chunks.data[chunk].n_waypoints[0]; i++)
        if (chunks.data[chunk].waypoints[0][i] == waypoint) {
            *side = 0;
            *index = i;
            return;
        }
    for (i = 0; i < chunks.data[chunk].n_waypoints[1]; i++)
        if (chunks.data[chunk].waypoints[1][i] == waypoint) {
            *side = 1;
            *index = i;
            return;
        }
#line 163
    fatal_error("%s(%d): Waypoint %d not found in chunk %d.\n", __FILE__, __LINE__, waypoint, chunk);
}

// Allocates the cop states (called with 40, discoveries). The name is ours.
// FUNCTION: SPEL 0x4025c0
void init_cop_ai_states(int n)
{
    cop_states.init(n);
}

// A new cop state: state 0, the player as the suspect. The name is ours.
// FUNCTION: SPEL 0x4026c0
int new_cop(int x)
{
    int i = cop_states.get_new();
    cop_states.data[i].state = 0;
    cop_states.data[i].suspect = player_object;
    cop_states.data[i].object = x;
    return i;
}

// A behaviour's preferred speed. The game's copy goes through the FPU (0x40292b), as an
// inline function's float result does; the function and its name are ours.
static float preferred_speed(int behaviour)
{
    return ai_behaviours.data[behaviour].preferred_speed;
}

// The police car's object handler (installed at 0x40c9a2), named by its trace line. With
// ai_model 1 it runs the cop's state machine: lookout, pursuit, got him, quitting, quit.
// FUNCTION: SPEL 0x402790
int HANDLE_COP_AI(int object)
{
    if (ai_model == 1) {
        trace("\nHANDLE_COP_AI, object %d.\n", object);
        int ai = vehicles.data[object].ai;
        int cop = ai_instances.data[ai].cop;
        int ret = 1;
        trace("target = %d.\n", cop_states.data[cop].suspect);
        trace("player = %d.\n", player_object);
        fn_4084b0(object);
        if (!ai_instances.data[ai].f2c) {
            switch (cop_states.data[cop].state) {
            case 0:
                trace("\nOn the lookout\n");
                ret = cop_lookout(object, cop);
                break;
            case 1:
                trace("Object %d, cop %d: ", object, cop);
                trace("\nIn pursuit of suspect\n");
                ret = cop_pursuit(object, cop);
                break;
            case 2:
                trace("\nGot him!\n");
                ai = vehicles.data[object].ai;
                ai_instances.data[ai].max_speed = vehicles.data[cop_states.data[cop].suspect].speed * 0.5f;
                if (game_mode != 8) {
                    release_player();
                    cop_states.data[cop].state = 3;
                    ai_instances.data[ai].behaviour = 2;
                    ai_instances.data[ai].max_speed = preferred_speed(3);
                    cop_states.data[cop].f0c = 0;
                }
                change_lane(object, 2, -1);
                ret = handle_basic_ai(object);
                break;
            case 3:
                trace("\nQuiting chase!\n");
                change_lane(object, 4, -1);
                ret = handle_basic_ai(object);
                break;
            case 4:
                trace("\nCalling it quits for today.\n");
                ret = handle_basic_ai(object);
                break;
            default:
#line 241
                fatal_error("%s(%d): Cop state %d corrupted!\n", __FILE__, __LINE__, cop);
                break;
            }
        }
        police_light_update();
        return ret;
    }
    fn_409060(object);
    if (!is_ai_object(object))
        fn_40fba0(object);
    return 0;
}

// The cop's lookout (state 0): parked until the suspect is within 2 chunks and offends; then
// the pursuit starts (state 1, behaviour 14 at its preferred speed, siren on).
// FUNCTION: SPEL 0x402a10
int cop_lookout(int object, int cop)
{
    trace("Cop-chunk = %d, target-chunk = %d.\n", vehicles.data[object].chunk,
          vehicles.data[cop_states.data[cop].suspect].chunk);
    int range = vehicles.data[cop_states.data[cop].suspect].chunk - vehicles.data[object].chunk;
    trace("range = %d.\n", range);
    if (range < 0)
        range = -range;
    trace("absolute range = %d.\n", range);
    if (range < 2 && cop_offence(object, cop, cop_states.data[cop].suspect) == 1) {
        trace("Starting pursuit...\n");
        int ai = vehicles.data[object].ai;
        cop_states.data[cop].state = 1;
        ai_instances.data[ai].behaviour = 14;
        ai_instances.data[ai].max_speed = preferred_speed(ai_instances.data[ai].behaviour);
        siren(object, 1);
        cop_states.data[cop].f0c = 0;
    }
    return 1;
}

// A cop's state. cop_pursuit adds the frame time to +0x0c through a base pointer
// (0x402b92 `lea esi, [eax+edi]`, then [esi+0xc]) that plain `cop_states.data[cop].f0c +=`
// does not give; an inline helper on that line does. Which helper the original had is not
// known: this accessor, or one taking &f0c, compile to the same bytes. The name is ours.
static t_cop &cop_state(int c)
{
    return cop_states.data[c];
}

// The cop's pursuit (state 1). After 40 s it quits (state 3). Ahead of the suspect on the
// other carriageway it slows to 0.8 of the suspect's speed; on the same side in another lane
// it moves a lane and slows to half; in the same lane exactly one chunk ahead, after 5 s and
// in game mode 0, it has caught the suspect: it stops, the siren goes off, the warning
// sequence (mode 8) starts and every other cop chasing the same suspect quits. Behind by more
// than 5 chunks it drives at its preferred speed, else at 1.25 of the suspect's, at least 15.
// FUNCTION: SPEL 0x402b40
int cop_pursuit(int object, int cop)
{
    int target_side, target_lane, my_side, my_lane;
    trace("Cop in pursuit of suspect!\n");
    trace("Suspect code number is %d.\nMatch with known criminal %d.\n",
          cop_states.data[cop].suspect, player_object);
    int suspect = cop_states.data[cop].suspect;
    int ai = vehicles.data[object].ai;
    cop_state(cop).f0c += game_timer.get_dt();
    cop_states.data[cop].timer += game_timer.get_dt();
    if (cop_states.data[cop].timer < 40) {
        trace("My chunk = %d, target chunk = %d.\n", vehicles.data[object].chunk,
              vehicles.data[suspect].chunk);
        if (vehicles.data[object].chunk > vehicles.data[suspect].chunk) {
            trace("Ahead of target!\n");
            waypoint_side(vehicles.data[suspect].chunk, vehicles.data[suspect].waypoint,
                          &target_side, &target_lane);
            trace("Target side = %d, lane = %d.\n", target_side, target_lane);
            waypoint_side(vehicles.data[object].chunk, vehicles.data[object].waypoint,
                          &my_side, &my_lane);
            cop_states.data[cop].target_lane = target_lane;
            if (target_side == my_side) {
                trace("Sides match...\n");
                if (target_lane == my_lane) {
                    if (vehicles.data[object].chunk - vehicles.data[suspect].chunk == 1
                        && cop_states.data[cop].f0c > 5 && game_mode == 0) {
                        cop_states.data[cop].state = 2;
                        cop_states.data[cop].f0c = 0;
                        ai_instances.data[ai].behaviour = 0;
                        vehicles.data[object].speed = vehicles.data[cop_states.data[cop].suspect].speed;
                        ai_instances.data[ai].max_speed = 0;
                        siren(object, 0);
                        set_game_mode(8);
                        for (int i = 0; i < cop_states.last_used(); i++) {
                            if (i != cop && cop_states.data[i].state == 1
                                && cop_states.data[i].suspect == cop_states.data[cop].suspect) {
                                cop_states.data[i].state = 3;
                                ai_instances.data[vehicles.data[cop_states.data[i].object].ai].behaviour = 2;
                                ai_instances.data[vehicles.data[cop_states.data[i].object].ai].max_speed =
                                    preferred_speed(3);
                                cop_states.data[i].f0c = 0;
                                siren(cop_states.data[i].object, 0);
                            }
                        }
                        pull_over_player();
                    }
                } else {
                    if (target_lane > ai_instances.data[ai].lane) {
                        trace("Target left...\n");
                        if (lane_left(ai) == 1)
                            ai_instances.data[ai].lane--;
                    } else {
                        trace("Target right...\n");
                        if (lane_left(ai) == 1)
                            ai_instances.data[ai].lane--;
                    }
                    ai_instances.data[ai].max_speed = vehicles.data[suspect].speed * 0.5f;
                }
            } else {
                trace("Match speed of target.\n");
                ai_instances.data[ai].max_speed = vehicles.data[suspect].speed * 0.8f;
            }
        } else if (vehicles.data[suspect].chunk - vehicles.data[object].chunk > 5) {
            trace("Speeding...\n");
            ai_instances.data[ai].max_speed = preferred_speed(ai_instances.data[ai].behaviour);
        } else {
            trace("Approaching...\n");
            ai_instances.data[ai].max_speed = vehicles.data[suspect].speed * 1.25f;
            if (ai_instances.data[ai].max_speed < 15)
                ai_instances.data[ai].max_speed = 15;
        }
    } else {
        cop_states.data[cop].state = 3;
        ai_instances.data[ai].behaviour = 2;
        ai_instances.data[ai].max_speed = preferred_speed(3);
        cop_states.data[cop].f0c = 0;
        siren(object, 0);
    }
    trace("Handling AI.\n");
    if (race_running && flag_4d9a78) {
        if (ai_instances.data[ai].f2c == 1) {
            ai_instances.data[ai].f30 += game_timer.get_dt();
            if (ai_instances.data[ai].f30 > 0.5f) {
                ai_instances.data[ai].f2c = 0;
                ai_instances.data[ai].f30 = 0;
                vehicles.data[object].f38 = 1;
            }
        }
        if (vehicles.data[object].f3c == 1) {
            ai_instances.data[ai].f2c = 1;
            ai_instances.data[ai].f30 = 0;
            vehicles.data[object].f38 = 0;
        }
        if (ai_instances.data[ai].f2c == 0) {
            trace("\tCalling change_lane.\n");
            change_lane(object, 2, -1);
            trace("\tCalling handle_basic_ai.\n");
            handle_basic_ai(object);
        }
        trace("\tCalling sndfx_play_object_sample.\n");
        sndfx_play_object_sample(object, 1);
        return 1;
    }
    return 1;
}

// The offence check: the target is on the other carriageway ("Ghost!") or faster than 1.1
// times the cop's limit. The lanes are only printed (and with %d for a float). 1 for an
// offence, else 0.
// FUNCTION: SPEL 0x403130
int cop_offence(int object, int cop, int target)
{
    int side, lane;
    trace("\nCop %d (object  %d), checking target %d.\n", cop, object, target);
    waypoint_side(vehicles.data[target].chunk, vehicles.data[target].waypoint, &side, &lane);
    trace("Target side: %d, lane %d.\n", side, lane);
    trace("Cop side: %d.\n", vehicles.data[object].side);
    if (vehicles.data[object].side != side) {
        trace("Ghost!\n");
        return 1;
    }
    trace("-> Side OK.\n");
    trace("Cop max lanes: %d.\n", cop_states.data[cop].max_lanes);
    trace("-> Lane OK.\n");
    trace("Cop max speed: %f.\n", cop_states.data[cop].max_speed * 1.1f);
    trace("Target speed: %f.\n", vehicles.data[target].speed);
    if (cop_states.data[cop].max_speed * 1.1f < vehicles.data[target].speed) {
        trace("Speeding!\n");
        return 1;
    }
    trace("-> Speed OK.\n");
    trace("No violation.\n");
    return 0;
}
