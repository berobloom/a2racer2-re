// ai_fsm.cpp: the AI state machine (discoveries "The state machine is table-driven"). The file
// is named by its asserts' __FILE__; it starts with its two rule tables, whose initialisers
// are its first functions (0x4032b0, 0x403300).
#include <stdlib.h>
#include "spel.h"

// A rule: in state `from`, when test(record) returns `result`, go to state `to`.
struct FSMRule {
    int from;                // +0x00
    int to;                  // +0x04
    int result;              // +0x08
    int (*test)(int idx);    // +0x0c

    FSMRule()
    {
        from = -1;
        to = -1;
        result = 0;
    }
};

// The racers' rules (records 0-2), filled at 0x409447. The names are ours.
// GLOBAL: SPEL 0x4a5410
FSMRule racer_rules[25];

// The initialiser of racer_rules (FSMRule's constructor inlined in a loop).
// SYNTHETIC: SPEL 0x4032b0
// $E2

// The compiler's vector constructor iterator, a COMDAT this object emits for the arrays
// (it calls a constructor n times). Its code is inlined here; other files call this copy
// (0x41aba6, 0x447280, ...), the first the linker met. That it sits between the two
// initialisers is what /Gy gives (one section per function, in the order emitted).
// SYNTHETIC: SPEL 0x4032d0
// ??_H@YGXPAXIHP6EX0@Z@Z

// The cops' rules (records 3-14), filled by 0x409200.
// GLOBAL: SPEL 0x4a5280
FSMRule cop_rules[25];

// The initialiser of cop_rules.
// SYNTHETIC: SPEL 0x403300
// $E5

// FUNCTION: SPEL 0x403320
void AIRecord::set_state(int s)
{
    target = -1;
    state = s;
    timer = 0;
    chunk_time = 0;
    laps = 0;
}

// Runs the state machine of every live record, once per frame; the dashboard's police light
// is updated once, at the first live cop. The name is ours.
// FUNCTION: SPEL 0x403340
void ai_fsm_update()
{
    int done = 0;
    for (int i = 0; i < 15; i++) {
        if (ai_records[i].object != -1) {
            ai_fsm_step(i);
            if (i >= 3 && !done) {
                police_light_update();
                done = 1;
            }
        }
    }
}

// One step of a record's state machine: its engine sample (off in state 9 for a cop), then the
// first rule that fires. The name is ours.
// FUNCTION: SPEL 0x403380
void ai_fsm_step(int idx)
{
    if (idx < 3)
        sndfx_play_object_sample(ai_records[idx].object, 1);
    else if (ai_records[idx].state == 9)
        sndfx_play_object_sample(ai_records[idx].object, 0);
    else
        sndfx_play_object_sample(ai_records[idx].object, 1);
    FSMRule *rules = idx < 3 ? racer_rules : cop_rules;
    int i = 0;
    while (i < 25) {
        if (rules[i].from == ai_records[idx].state && rules[i].result == rules[i].test(idx)) {
            ai_records[idx].state = rules[i].to;
            i = 25;
        } else
            i++;
    }
}

// Sets racer rule i; 0 when i is out of range. The name is ours.
// FUNCTION: SPEL 0x403420
int ai_fsm_racer_rule(int i, int from, int to, int result, int (*test)(int))
{
    if (i >= 25)
        return 0;
    racer_rules[i].from = from;
    racer_rules[i].to = to;
    racer_rules[i].result = result;
    racer_rules[i].test = test;
    return 1;
}

// Sets cop rule i; 0 when i is out of range. The name is ours.
// FUNCTION: SPEL 0x403460
int ai_fsm_cop_rule(int i, int from, int to, int result, int (*test)(int))
{
    if (i >= 25)
        return 0;
    cop_rules[i].from = from;
    cop_rules[i].to = to;
    cop_rules[i].result = result;
    cop_rules[i].test = test;
    return 1;
}

// Opens a chase: a free cop record (state 3) for the police car `object` after `target`,
// unless the target already has its maximum of cops (the player 1 on the easy difficulty and
// 3 on the hard one, a racer 2). On the city stages only with a chance of about dt per frame.
// The radio speaks and the siren goes on. 1 when it opened one. The name is ours.
// FUNCTION: SPEL 0x4034a0
int ai_start_chase(int object, int target)
{
    int i = 3;
    int max = target == player_object ? (difficulty ? 3 : 1) : 2;
    if (tracks[etappe].city == 1) {
        float chance = game_timer.get_dt() * 1000;
        if (rand() % 1000 > chance)
            return 0;
    }
    for (; i < 15; i++)
        if (ai_records[i].object == -1)
            break;
    if (ai_records[i].object == -1) {
        int n = 0;
        for (int j = 3; j < 15; j++)
            if (ai_records[j].target == target)
                n++;
        if (n < max) {
            ai_records[i].object = object;
            ai_records[i].state = 3;
            ai_records[i].target = target;
            ai_records[i].control.car_type = player_car;
            radio_pursuit(target);
            siren(object, 1);
            return 1;
        }
    }
    return 0;
}

// Frees a cop's record and takes it off the siren list. The name is ours.
// FUNCTION: SPEL 0x4035b0
void ai_free_cop(int idx)
{
    if (idx < 3) {
#line 396
        fatal_error("Illegal cop, this is a racer: %s %d", __FILE__, __LINE__);
        return;
    }
    for (int i = 0; i < 20; i++)
        if (siren_list[i] == ai_records[idx].object) {
            siren_list[i] = -1;
            i = 20;
        }
    ai_records[idx].clear();
}

// Is the object one of the AI records'? The name is ours.
// FUNCTION: SPEL 0x403630
int is_ai_object(int object)
{
    int i;
    for (i = 0; i < 15; i++)
        if (ai_records[i].object == object)
            break;
    return i != 15;
}
