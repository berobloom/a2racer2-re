// spel.h: the types, globals and functions of spel.dat that src/spel/*.cpp share.
// Each address annotation names where the thing is in spel.dat:
//   // FUNCTION: SPEL 0xADDR   before a function (its definition is compared byte for byte;
//                              a prototype only tells the check where the function is)
//   // GLOBAL: SPEL 0xADDR     before a global variable
// The evidence for names and layouts is in research/discoveries.md.
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
    char pad00[0x18];
    float max_speed;         // +0x18 ai_max_speed_allowed
    float chosen_speed;      // +0x1c ai_chosen_speed
    char pad20[4];
    int behaviour;           // +0x24 index into ai_behaviours
    char pad28[4];
    int f2c;                 // +0x2c 0: "No collision" (the ADAC handler's log)
    char pad30[0x1c];
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
    char pad1c[0x50];
    float heading;           // +0x6c
    char pad70[4];
    float speed;             // +0x74
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

// The debug trace: printf-style, to debug.txt when Raser.ini DEBUGON=1.
// FUNCTION: SPEL 0x464ca0
void trace(const char *fmt, ...);

// HANDLE_TRAFFIC_AI, named by its own trace lines (discoveries).
// FUNCTION: SPEL 0x40b770
int handle_traffic_ai(int object);

// handle_basic_ai, named by the trace line before its calls.
// FUNCTION: SPEL 0x4076b0
void handle_basic_ai(int object);

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
