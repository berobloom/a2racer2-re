// unplaced.cpp: functions of spel.dat whose source file is not known yet.
// reverse-engineering/analysis/spel_modules.tsv says which file each function is from when a
// __FILE__ string or its neighbours tell; these are code before ai_cfun.cpp, so from a file
// that sorts before it (the objects are linked in alphabetical order).
#include "spel.h"

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
    ai_records[idx].steer = angle > 200 ? 100.0f : angle < -200 ? -100.0f : angle * 0.5f;
}
