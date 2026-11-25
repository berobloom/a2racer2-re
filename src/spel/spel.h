// spel.h: the types, globals and functions of spel.dat that src/spel/*.cpp share.
// Each address annotation names where the thing is in spel.dat:
//   // FUNCTION: SPEL 0xADDR   before a function (its definition is compared byte for byte;
//                              a prototype only tells the check where the function is)
//   // GLOBAL: SPEL 0xADDR     before a global variable
// The evidence for names and layouts is in research/discoveries.md.
#include <array.hpp>
#ifndef SPEL_H
#define SPEL_H

// The driving controls of a computer car: the second half of its AIRecord (+0x30). The
// method at 0x401200 is called with this = record + 0x30 (0x4093c3, 0x409428).
struct AIControl {
    int car_type;            // +0x00 (record +0x30) car type at spawn
    float steer;             // +0x04 (+0x34) -100..100
    float throttle;          // +0x08 (+0x38) 0..100
    float brake;             // +0x0c (+0x3c) 0..100
    int coast;               // +0x10 (+0x40) engine off
    int gear;                // +0x14 (+0x44)
    float drift_timer;       // +0x18 (+0x48)
    float drift_sum;         // +0x1c (+0x4c)
    float drift_start;       // +0x20 (+0x50)
    float f24;               // +0x24 (+0x54)
    int stopped;             // +0x28 (+0x58)
    int f2c;                 // +0x2c (+0x5c)

    // Everything back to 0 but the car type. The name is ours.
    // FUNCTION: SPEL 0x401200
    void reset();
};

// One computer-driven car's record: 15 of them at 0x4a55a0, 96 bytes each
// (discoveries.md "The record (15 x 96 bytes at 0x4a55a0)").
struct AIRecord {
    int object;              // +0x00 object index, -1 = free
    int state;               // +0x04
    int target;              // +0x08
    int laps;                // +0x0c
    float timer;             // +0x10
    float chunk_time;        // +0x14
    float reverse_timer;     // +0x18
    int waypoint;            // +0x1c
    int chunk;               // +0x20
    int prev_chunk;          // +0x24
    int finish_flags;        // +0x28
    float cop_speed_limit;   // +0x2c
    AIControl control;       // +0x30

    // A fresh state: no target, timers and laps cleared (called at 0x4093cf). The name is
    // ours.
    // FUNCTION: SPEL 0x403320
    void set_state(int s);

    // Frees the record (inline in ai_free_cop 0x4035b0). The name is ours.
    void clear()
    {
        object = -1;
        state = -1;
        target = -1;
        timer = 0;
        chunk_time = 0;
        laps = 0;
    }
};

// GLOBAL: SPEL 0x4a55a0
extern AIRecord ai_records[15];

// The three racers' slots, 88 bytes each (discoveries "results": +0 car, +4 time,
// +8 position, +0xc vehicle object, +0x10 finished).
struct RacerSlot {
    int car;                 // +0x00
    int time;                // +0x04
    int position;            // +0x08 0 = leading
    int object;              // +0x0c
    int finished;            // +0x10
    char pad14[0x44];
};

// GLOBAL: SPEL 0x485b20
extern RacerSlot racer_slots[3];

// The AI instance records, 108 bytes each (discoveries "The AI instance records").
struct AIInstance {
    char pad00[0x14];
    int lane;                // +0x14 the lane it drives in (cop_pursuit moves it down by one)
    float max_speed;         // +0x18 ai_max_speed_allowed
    float chosen_speed;      // +0x1c ai_chosen_speed
    char pad20[4];
    int behaviour;           // +0x24 index into ai_behaviours
    char pad28[4];
    int f2c;                 // +0x2c 0: "No collision" (the ADAC handler's log)
    float f30;               // +0x30 how long f2c has been 1; cleared after 0.5 s
    char pad34[0x10];
    int cop;                 // +0x44 a police car's index into cop_states
    char pad48[4];
    int sample_loop;         // +0x4c passed to sndfx_play_object_sample
    char pad50[0x10];
    int progress;            // +0x60 chunks travelled
    char pad64[8];
};

struct AIInstanceArray {
    AIInstance *data;
};

// GLOBAL: SPEL 0x4a5b40
extern AIInstanceArray ai_instances;

// "AI Behaviors[16]", 44 bytes each, built by init_ai_states 0x405bc0 (discoveries "The
// behaviour table, by emulation").
struct AIBehaviour {
    char pad00[8];
    float brake_speed;       // +0x08 bh_brake_speed
    char pad0c[4];
    float acceleration;      // +0x10
    float preferred_speed;   // +0x14 bh_preferred_speed
    float deceleration;      // +0x18
    char pad1c[0x10];
};

struct AIBehaviourArray {
    AIBehaviour *data;
};

// GLOBAL: SPEL 0x4a5b88
extern AIBehaviourArray ai_behaviours;

// The rubber band's parameters by difficulty and stage, the row repeated for each of the
// three racers (port/src/ai_car.c RUBBER).
struct RubberRow {
    float upper;             // lead above which the racer is slowed
    float lower;             // lead below which it is sped up
    float gain;
};

// GLOBAL: SPEL 0x482258
extern RubberRow rubber_band[2][7][3];

// [OPTIONS] difficulty (discoveries, 0x421e32).
// GLOBAL: SPEL 0x4d9ab0
extern int difficulty;

// The stage number (etappe).
// GLOBAL: SPEL 0x4d9a74
extern int etappe;

// The player's progress, chunks driven since the start.
// GLOBAL: SPEL 0x4d9e70
extern int player_progress;

// The debug trace: printf-style, to debug.txt when Raser.ini DEBUGON=1.
// FUNCTION: SPEL 0x464ca0
void trace(const char *fmt, ...);

// A cop's state (discoveries "the active cops live in a T_ARRAY").
struct t_cop {
    int state;               // +0x00
    int suspect;             // +0x04
    int target_lane;         // +0x08 the suspect's lane, written while ahead of it
    float f0c;               // +0x0c
    float timer;             // +0x10 pursuit timer
    float max_speed;         // +0x14 the limit it enforces
    float max_lanes;         // +0x18
    int object;              // +0x1c the police car, given to new_cop
};

// GLOBAL: SPEL 0x4a5238
extern T_ARRAY<t_cop> cop_states;

// The engine's runtime road chunk, 388 bytes (discoveries, the trigger dispatcher).
struct Chunk {
    char pad00[0xe4];
    int n_waypoints[2];      // +0xe4 waypoints on each carriageway
    int trigger[2];          // +0xec the .pmp trigger slots
    char padf4[0x30];
    int waypoints[2][12];    // +0x124 their numbers
};

struct ChunkArray {
    Chunk *data;
};

// GLOBAL: SPEL 0x4e65e0
extern ChunkArray chunks;

// The last speed and lane signs behind the vehicle (discoveries "BACKTRACK_SPEED_TRIGGER").
// FUNCTION: SPEL 0x408080
void backtrack_speed_trigger(int object, int *speed, int *lane);

// Game.ini [OPTIONS] ai_model: 1 the old (Autobahn Raser I) AI, 0 the new; shipped 0
// (discoveries "Two AI models").
// GLOBAL: SPEL 0x4d9b10
extern int ai_model;

// The cop states' lookout and pursuit (discoveries, HANDLE_COP_AI). The names are ours.
// FUNCTION: SPEL 0x402a10
int cop_lookout(int object, int cop);
// FUNCTION: SPEL 0x402b40
int cop_pursuit(int object, int cop);

// The offence check: does the suspect break the cop's rules (side, lanes, speed)?
// 1 when it does (discoveries "the offence check 0x403130"). The name is ours.
// FUNCTION: SPEL 0x403130
int cop_offence(int object, int cop, int suspect);

// Moves the AI car's waypoint to the next lane on the left; 1 when it did (discoveries,
// "The switch retargets the waypoint"). The name is ours.
// FUNCTION: SPEL 0x406d90
int lane_left(int ai);

// Sets the game mode and starts its sequence (discoveries, 0x424d20). The name is ours.
// FUNCTION: SPEL 0x424d20
void set_game_mode(int mode);

// After a catch: gives the player's car an AI driver (discoveries, "a catch pulls the player
// over"). The name is ours.
// FUNCTION: SPEL 0x4258f0
void pull_over_player();

// change_lane (discoveries "Traffic lane choice and following").
// FUNCTION: SPEL 0x403660
void change_lane(int object, int lanes, int x);

// Gives the player's car back after a catch (discoveries, "a catch pulls the player over").
// FUNCTION: SPEL 0x425980
void release_player();

// Flashes the police light on the dashboard while a siren is on (discoveries, 0x40c860).
// FUNCTION: SPEL 0x40c860
void police_light_update();

// The old model's cop: called by HANDLE_COP_AI when ai_model is 0; not known yet.
// FUNCTION: SPEL 0x409060
void fn_409060(int object);
// FUNCTION: SPEL 0x40fba0
void fn_40fba0(int object);

// A row of the track table (Game.ini's track list, 137 bytes each; discoveries "the array
// base is 0x4D8E9C").
#pragma pack(push, 1)
struct Track {
    int city;                // +0x00 1 on the city stages
    char pad04[133];
};
#pragma pack(pop)

// GLOBAL: SPEL 0x4d8e9c
extern Track tracks[20];

// The player's car type, 0..6 (discoveries).
// GLOBAL: SPEL 0x4d9e2c
extern int player_car;

// The objects of the cops whose siren is on, -1 for a free entry; the radar shows them
// (discoveries, "the siren list 0x4a5ce0").
// GLOBAL: SPEL 0x4a5ce0
extern int siren_list[20];

// The police radio's "achter" line, as a pursuit starts (discoveries, session 12).
// FUNCTION: SPEL 0x4150f0
void radio_pursuit(int target);

// Is the object one of the AI records'? (ai_fsm.cpp)
// FUNCTION: SPEL 0x403630
int is_ai_object(int object);

// One step of a record's state machine (ai_fsm.cpp).
// FUNCTION: SPEL 0x403380
void ai_fsm_step(int idx);

// A runtime waypoint, 80 bytes (discoveries "the runtime waypoint"): its .dmp links.
struct Waypoint {
    char pad00[0x14];
    int next;                // +0x14 L0: the same lane, next chunk
    int lane_up;             // +0x18 L2: lane + 1
    int lane_down;           // +0x1c L1: lane - 1
    int prev;                // +0x20 L3: the previous chunk
    char pad24[0x2c];
};

struct WaypointArray {
    Waypoint *data;
};

// GLOBAL: SPEL 0x4e6628
extern WaypointArray waypoints;

// What change_lane learns about one lane; the field names are the game's own (its trace at
// 0x403a84, discoveries "cop_lane").
struct t_lane_info {
    int m_dead_end;          // +0x00
    int m_dead_range;        // +0x04
    int m_front_obstacle;    // +0x08 vehicle, -1 none
    float m_front_range;     // +0x0c
    int m_front_status;      // +0x10
    int m_rear_obstacle;     // +0x14
    float m_rear_range;      // +0x18
    int m_rear_status;       // +0x1c
};

// The game mode, set by 0x424d20 (discoveries): 0 racing, 8 the police sequence, ...
// GLOBAL: SPEL 0x4d9a7c
extern int game_mode;

// The player's race position (0x408c00, discoveries "results").
// GLOBAL: SPEL 0x4d9e60
extern int player_position;

// Twice this is a cop's limit before the first speed sign; LIKELY the player car's top
// speed (discoveries "What trigger 8's \"2x default\" is").
// GLOBAL: SPEL 0x4d9e68
extern float player_top_speed;

// The player's object index.
// GLOBAL: SPEL 0x4d9e28
extern int player_object;

// A live vehicle: 160 bytes each in the array at 0x4e48e0 (discoveries "CARS.DAT A[29]").
struct Vehicle {
    char pad00[8];
    int d3d;                 // +0x08 index into d3d_objects
    int ai;                  // +0x0c index into ai_instances
    char pad10[8];
    int chunk;               // +0x18 the road chunk it is on
    char pad1c[4];
    int waypoint;            // +0x20 its waypoint this frame (port/src/racers.c)
    char pad24[4];
    int side;                // +0x28 its carriageway, 1 or 0 (RESET_COP, discoveries)
    char pad2c[0xc];
    int f38;                 // +0x38 set again 0.5 s after AIInstance f2c (cop_pursuit)
    int f3c;                 // +0x3c 1 sets AIInstance f2c (cop_pursuit)
    char pad40[0x2c];
    float lateral;           // +0x6c sideways velocity (the cop rules: |v_fwd| + |v_lat|)
    char pad70[4];
    float speed;             // +0x74 forward velocity
    char pad78[4];
    float yaw_rate;          // +0x7c
    char pad80[0x20];
};

// dplib's T_ARRAY (c:\visual~1\dplib\array.hpp): the element pointer comes first.
// The rest of the layout is not known yet.
struct VehicleArray {
    Vehicle *data;
};

// GLOBAL: SPEL 0x4e48e0
extern VehicleArray vehicles;

// dplib's c_3dobject, 153 bytes each (the T_ARRAY "_d3d_objects" at 0x4ea9e0; its constructor
// names the type, discoveries "CARS.DAT A[29]"). 153 is not a multiple of 4: packed.
#pragma pack(push, 1)
struct C3dObject {
    char pad00[0x50];
    float x;                 // +0x50
    float y;                 // +0x54
    float z;                 // +0x58
    char pad5c[0x1c];
    float yaw;               // +0x78 0..4096 = one turn
    char pad7c[0x1d];
};
#pragma pack(pop)

struct C3dObjectArray {
    C3dObject *data;
};

// GLOBAL: SPEL 0x4ea9e0
extern C3dObjectArray d3d_objects;

// The game's timer (tmg_main.cpp). The field at +0x0c is the last frame's time.
struct Timer {
    char pad00[0x0c];
    float frame_time;        // +0x0c

    // The frame time for the game's movement: 0.1 s when it is 0 or above a limit.
    // FUNCTION: SPEL 0x441cd0
    float get_dt();
};

// GLOBAL: SPEL 0x4e4970
extern Timer game_timer;

// LIKELY a fatal error: formats its arguments (vsprintf), logs "[%d] %s\n", shows a
// message box and stops the game.
// FUNCTION: SPEL 0x464e50
void fatal_error(const char *fmt, ...);

// The angle from an object to a point, -2048..2048 (port/src/ai_car.h a2_ai_rel_angle).
// FUNCTION: SPEL 0x40fe90
int rel_angle(int object, const float *point);

// The ADAC (roadside assistance) car's state: 1 once it has stopped by the player.
// GLOBAL: SPEL 0x4a5cc8
extern int adac_state;

// Set to 1 by the race's game state 0 and cleared by state 4 (discoveries, 0x424d68 /
// 0x424da6): LIKELY "the race is running".
// GLOBAL: SPEL 0x4d9a9c
extern int race_running;

// Cleared with race_running by state 4 (0x424da6); not known yet.
// GLOBAL: SPEL 0x4d9a78
extern int flag_4d9a78;

// HANDLE_TRAFFIC_AI, named by its own trace lines (discoveries).
// FUNCTION: SPEL 0x40b770
int handle_traffic_ai(int object);

// handle_basic_ai, named by the trace line before its calls.
// FUNCTION: SPEL 0x4076b0
int handle_basic_ai(int object);

// sndfx_play_object_sample(obj, loop), named by the trace line before its calls.
// FUNCTION: SPEL 0x428bc0
void sndfx_play_object_sample(int object, int loop);

// Slot 15's GetTickCount flasher (discoveries: 0x40c860 and 0x40c4a0).
// FUNCTION: SPEL 0x40c4a0
void fn_40c4a0();

// Called first by the ADAC handler; not known yet.
// FUNCTION: SPEL 0x4084b0
void fn_4084b0(int object);

// Returns a float the ADAC handler stores as the car's height (c_3dobject +0x54).
// FUNCTION: SPEL 0x40ede0
float fn_40ede0(int object);

// A police car's light on or off; it also swaps its sample to psirene1.wav (discoveries).
// FUNCTION: SPEL 0x40c640
void siren(int object, int on);

// "Faces along the road" (discoveries, the cop rules): 0 when the object does not.
// FUNCTION: SPEL 0x410a10
int faces_road(int object);

// The lane of the object's waypoint it drives with, -1 when it drives against it
// (discoveries, the cop rules).
// FUNCTION: SPEL 0x40a100
int waypoint_lane(int object, int waypoint);

// The distance from one chunk to another, signed (discoveries, 0x409540).
// FUNCTION: SPEL 0x442700
int chunk_distance(int from, int to);

// The police radio's "opgev" (give up) line (discoveries, session 12).
// FUNCTION: SPEL 0x415170
void radio_give_up(int object);

// Called by ai_drive for the record or the object; not known yet.
// FUNCTION: SPEL 0x40ff90
void fn_40ff90(int idx);
// FUNCTION: SPEL 0x410280
void fn_410280(int idx);
// FUNCTION: SPEL 0x40ebd0
void fn_40ebd0(int object);
// FUNCTION: SPEL 0x40ece0
void fn_40ece0(int object);
// FUNCTION: SPEL 0x40ee80
void fn_40ee80(int object);

#endif
