// ai_cops.cpp: the police AI (discoveries "the active cops", "RESET_COP, BACKTRACK_SPEED_TRIGGER
// and cop_lane"). The file is named by its asserts' __FILE__.
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
    cop_states.data[i].f1c = x;
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
    if (!fn_403630(object))
        fn_40fba0(object);
    return 0;
}
