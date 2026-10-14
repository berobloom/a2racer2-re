/* The engine's box contact test; see contact.h. */
#include "contact.h"

#include <math.h>

#define TURN 4096.0f

/* The engine's turn (0x44aef0, 0x44b860): x' = x c - z s, z' = x s + z c,
 * at a whole table step. */
static void rot(float x, float z, float heading, float *ox, float *oz)
{
    int i = ((int)heading) & 4095;
    float a = (float)i * (6.28318531f / TURN);
    float c = cosf(a), s = sinf(a);
    *ox = x * c - z * s;
    *oz = x * s + z * c;
}

static void corner_of(const a2_cbox *b, int k, float *x, float *z)
{
    *x = (k == 1 || k == 2) ? b->x1 : b->x0;
    *z = (k >= 2) ? b->z1 : b->z0;
}

static int inside(const a2_cbox *a, float px, float pz)
{
    return px >= a->x0 && px <= a->x1 && pz >= a->z0 && pz <= a->z1;
}

/* the side of A nearest a point inside it */
static int nearest_side(const a2_cbox *a, float px, float pz)
{
    float d[4] = {pz - a->z0, a->x1 - px, a->z1 - pz, px - a->x0};
    int best = 0;
    for (int k = 1; k < 4; k++)
        if (d[k] < d[best])
            best = k;
    return best;
}

/* the segment's entry into A through one of its sides */
static int entry(const a2_cbox *a, float x0, float z0, float x1, float z1, float *t, int *side)
{
    float dx = x1 - x0, dz = z1 - z0;
    int found = 0;
    for (int k = 0; k < 4; k++) {
        int along_z = k == 0 || k == 2;
        float bound = k == 0 ? a->z0 : k == 1 ? a->x1 : k == 2 ? a->z1 : a->x0;
        float sgn = (k == 1 || k == 2) ? 1.0f : -1.0f;
        float d = along_z ? dz : dx, s = along_z ? z0 : x0;
        if (d == 0.0f || (s - bound) * sgn < 0.0f)
            continue;
        float tk = (bound - s) / d;
        if (tk < 0.0f || tk > 1.0f)
            continue;
        float o = along_z ? x0 + dx * tk : z0 + dz * tk;
        float lo = along_z ? a->x0 : a->z0, hi = along_z ? a->x1 : a->z1;
        if (o < lo || o > hi)
            continue;
        if (!found || tk < *t) {
            *t = tk;
            *side = k;
            found = 1;
        }
    }
    return found;
}

int a2_contact_sweep(const a2_cbox *a, const a2_cbox *b, float mx, float mz, float *t,
                     int *corner, int *side)
{
    float best = 1.0f;
    int hit = 0;
    for (int k = 0; k < 4; k++) {
        float cx, cz, wx, wz, lx, lz;
        corner_of(b, k, &cx, &cz);
        rot(cx, cz, b->heading, &wx, &wz);
        rot(wx + b->x - a->x, wz + b->z - a->z, -a->heading, &lx, &lz);
        float tk;
        int sk;
        if (inside(a, lx, lz)) {
            if (!inside(a, lx + mx, lz + mz))
                continue; /* leaving: no contact */
            tk = 0.0f;
            sk = nearest_side(a, lx, lz);
        } else if (!entry(a, lx, lz, lx + mx, lz + mz, &tk, &sk)) {
            continue;
        }
        if (tk < best) {
            best = tk;
            *corner = k;
            *side = sk;
            hit = 1;
        }
    }
    *t = best;
    return hit;
}

/* 0x44b860: a side's outward normal, in the world */
static void side_normal(const a2_cbox *a, int side, float n[2])
{
    static const float N[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};
    rot(N[side][0], N[side][1], a->heading, &n[0], &n[1]);
}

int a2_contact(const a2_cbox *self, float dx, float dz, const a2_cbox *other, float *t,
               float point[2], float normal[2])
{
    float best = 1.0f;
    int hit = 0;
    float t1, mx, mz;
    int c1, s1;
    /* 1: our corners into the other box, the move in its frame */
    rot(dx, dz, -other->heading, &mx, &mz);
    if (a2_contact_sweep(other, self, mx, mz, &t1, &c1, &s1) && t1 < best) {
        best = t1;
        float cx, cz, wx, wz;
        corner_of(self, c1, &cx, &cz);
        rot(cx, cz, self->heading, &wx, &wz);
        point[0] = self->x + wx + dx * t1;
        point[1] = self->z + wz + dz * t1;
        side_normal(other, s1, normal);
        normal[0] = -normal[0]; /* its outward normal points back at us */
        normal[1] = -normal[1];
        hit = 1;
    }
    /* 2: its corners into our box, against the move, in our frame */
    rot(-dx, -dz, -self->heading, &mx, &mz);
    if (a2_contact_sweep(self, other, mx, mz, &t1, &c1, &s1) && t1 < best) {
        best = t1;
        float cx, cz, wx, wz;
        corner_of(other, c1, &cx, &cz);
        rot(cx, cz, other->heading, &wx, &wz);
        point[0] = other->x + wx;
        point[1] = other->z + wz;
        side_normal(self, s1, normal);
        hit = 1;
    }
    *t = best;
    return hit;
}
