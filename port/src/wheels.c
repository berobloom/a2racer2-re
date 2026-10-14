/* The racing cars' wheels, after car_whl.cpp; see wheels.h. */
#include "wheels.h"

#include <math.h>

/* 0x486198: per car type, the wheels' positions in the car's frame (front
 * left, front right, rear left, rear right; +Z is the nose). */
static const float WHEEL_POS[A2_WHEEL_TYPES][4][3] = {
    {{-7.0f, 3.7f, 13.3f}, {7.0f, 3.7f, 13.3f}, {-7.0f, 3.7f, -10.0f}, {7.0f, 3.7f, -10.0f}},
    {{-7.4f, 3.7f, 13.8f}, {7.4f, 3.7f, 13.8f}, {-7.5f, 3.7f, -14.5f}, {7.5f, 3.7f, -14.5f}},
    {{-7.15f, 3.7f, 11.2f}, {7.15f, 3.7f, 11.2f}, {-7.15f, 3.7f, -11.72f}, {7.15f, 3.7f, -11.72f}},
    {{-7.0f, 3.7f, 12.6f}, {7.0f, 3.7f, 12.6f}, {-6.6f, 3.7f, -12.5f}, {6.6f, 3.7f, -12.5f}},
    {{-7.75f, 3.7f, 13.0f}, {7.75f, 3.7f, 13.0f}, {-7.5f, 3.7f, -12.5f}, {7.5f, 3.7f, -12.5f}},
    {{-7.83f, 3.7f, 13.5f}, {7.83f, 3.7f, 13.5f}, {-7.83f, 3.7f, -13.0f}, {7.83f, 3.7f, -13.0f}},
    {{-8.0f, 3.7f, 14.0f}, {8.0f, 3.7f, 14.0f}, {-8.3f, 3.7f, -14.8f}, {8.3f, 3.7f, -14.8f}},
};

/* [0x49b014], the sine table's size (4096 at runtime, read under Wine). */
#define TURN 4096.0f

void a2_wheels_init(a2_wheels *w)
{
    w->spin = 0.0f;
    w->steer = 0.0f;
    w->y = 3.7f;
}

/* Shared by both: roll at the speed of a radius-3.5 wheel, at most just under
 * half a turn a frame (0.497213), and stand on the road: the road height under
 * the car minus the car's own, held to +-1.05, plus the radius. */
static void roll(a2_wheels *w, float v_fwd, float ground_minus_y, float dt)
{
    float d = dt * (TURN * 10.0f / ((float)M_PI * 7.0f)) * v_fwd;
    float cap = TURN * 0.497213f;
    if (d > cap)
        d = cap;
    w->spin -= d;
    float h = ground_minus_y;
    if (h > 1.05f)
        h = 1.05f;
    if (h < -1.05f)
        h = -1.05f;
    w->y = h + 3.5f;
}

void a2_wheels_update_player(a2_wheels *w, float v_fwd, float steer_input,
                             float ground_minus_y, float dt)
{
    roll(w, v_fwd, ground_minus_y, dt);
    w->steer = TURN * 0.0833333f * -0.01f * steer_input; /* full lock: a twelfth of a turn */
}

void a2_wheels_update_ai(a2_wheels *w, float v_fwd, float yaw_rate, float ground_minus_y,
                         float dt)
{
    roll(w, v_fwd, ground_minus_y, dt);
    float s = yaw_rate * 400.0f;
    if (s > 500.0f)
        s = 500.0f;
    if (s < -500.0f)
        s = -500.0f;
    w->steer = s;
}

void a2_wheels_draw(const a2_wheels *w, const a2_scene *s, int wheel_model, int car_type)
{
    if (wheel_model < 0 || car_type < 0 || car_type >= A2_WHEEL_TYPES)
        return;
    /* The angles as the car body takes them: a heading h is drawn as a yaw of
     * -h and a pitch p as +p (degrees). */
    float spin = w->spin * (360.0f / TURN);
    float steer = -w->steer * (360.0f / TURN);
    for (int i = 0; i < 4; i++) {
        const float *p = WHEEL_POS[car_type][i];
        a2_scene_draw_model_rpy(s, wheel_model, p[0], w->y, p[2], i < 2 ? steer : 0.0f, spin, 0.0f);
    }
}
