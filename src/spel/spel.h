// spel.h: the types, globals and functions of spel.dat that src/spel/*.cpp share.
// Each address annotation names where the thing is in spel.dat:
//   // FUNCTION: SPEL 0xADDR   before a function (its definition is compared byte for byte;
//                              a prototype only tells the check where the function is)
//   // GLOBAL: SPEL 0xADDR     before a global variable
// The evidence for names and layouts is in research/discoveries.md.
#ifndef SPEL_H
#define SPEL_H

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
    int car_type;            // +0x30
    float steer;             // +0x34 -100..100
    float throttle;          // +0x38 0..100
    float brake;             // +0x3c 0..100
    int coast;               // +0x40
    int gear;                // +0x44
    float drift_timer;       // +0x48
    float drift_sum;         // +0x4c
    float drift_start;       // +0x50
    int pad54;               // +0x54
    int stopped;             // +0x58
    int pad5c;               // +0x5c
};

// GLOBAL: SPEL 0x4a55a0
extern AIRecord ai_records[15];

// LIKELY a fatal error: formats its arguments (vsprintf), logs "[%d] %s\n", shows a
// message box and stops the game.
// FUNCTION: SPEL 0x464e50
void fatal_error(const char *fmt, ...);

// The angle from an object to a point, -2048..2048 (port/src/ai_car.h a2_ai_rel_angle).
// FUNCTION: SPEL 0x40fe90
int rel_angle(int object, const float *point);

#endif
