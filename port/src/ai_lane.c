#include "ai_lane.h"

#include <math.h>
#include <string.h>

void a2_ai_scan_lanes(const a2_ai_vehicle *self, const a2_ai_vehicle *v, int nv, int lanes,
                      a2_lane_info *L)
{
    for (int k = 0; k < lanes; k++) {
        memset(&L[k], 0, sizeof L[k]);
        int best_f = -1, best_r = -1, df = 99, dr = 99;
        float rf = 0.0f, rr = 0.0f;
        for (int i = 0; i < nv; i++) {
            const a2_ai_vehicle *o = &v[i];
            if (o->lane != k || o->oncoming != self->oncoming ||
                (o->x == self->x && o->z == self->z))
                continue;
            int d = self->oncoming ? self->chunk - o->chunk : o->chunk - self->chunk;
            float dx = o->x - self->x, dy = o->y - self->y, dz = o->z - self->z;
            float r = sqrtf(dx * dx + dy * dy + dz * dz);
            int ahead = d > 0 || (d == 0 && o->t > self->t);
            /* the first waypoint holding something, then the nearest on it */
            if (ahead && d <= A2_AI_SCAN_AHEAD) {
                if (d < df || (d == df && r < rf)) {
                    df = d;
                    rf = r;
                    best_f = i;
                }
            } else if (!ahead && -d <= A2_AI_SCAN_BEHIND) {
                if (-d < dr || (-d == dr && r < rr)) {
                    dr = -d;
                    rr = r;
                    best_r = i;
                }
            }
        }
        if (best_f >= 0) {
            L[k].front = v[best_f].speed > self->speed ? 1 : 2;
            L[k].front_range = rf;
            L[k].front_speed = v[best_f].speed;
        }
        if (best_r >= 0) {
            L[k].rear = v[best_r].speed > self->speed ? 1 : 2;
            L[k].rear_range = rr;
        }
    }
}

float a2_ai_lane_speed(const a2_lane_info *l, float preferred, float close, float follow,
                       float speed)
{
    switch (l->front) {
    case 1:
    case 2:
        if (close > l->front_range)
            return l->front_speed * 0.75f;
        if (l->front_range < follow)
            return l->front_speed;
        return preferred;
    case 3: {
        float s = speed * 0.75f;
        return s < 2.0f ? 2.0f : s;
    }
    case 4: return 2.0f;
    default: return preferred;
    }
}

int a2_ai_race_lane(const a2_lane_info *L, int lanes, int cur, float rear_gap, float front_gap)
{
    /* keep the lanes with room behind, then those free ahead or whose
     * vehicle ahead (status 1 or 2) is beyond the front gap */
    int cand[16], nc = 0;
    for (int k = 0; k < lanes && k < 16; k++) {
        if (!(L[k].rear == 0 || rear_gap < L[k].rear_range))
            continue;
        if (L[k].front == 0 || (L[k].front > 0 && L[k].front < 3 && front_gap < L[k].front_range))
            cand[nc++] = k;
    }
    if (nc == 0)
        return cur;
    /* the last candidate first, then the lowest status; equal faster/slower
     * statuses go to the longer range (0x405144..0x405195) */
    int pick = cand[nc - 1];
    for (int j = 0; j < nc - 1; j++) {
        int k = cand[j];
        if ((L[pick].front == 1 || L[pick].front == 2) && L[k].front == L[pick].front &&
            L[k].front_range > L[pick].front_range)
            pick = k;
        if (L[k].front < L[pick].front)
            pick = k;
    }
    return pick;
}
