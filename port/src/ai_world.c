#include "ai_world.h"

#include "ai_car.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OCC_MAX 10 /* rd_drmap.cpp's "obstacle waypoint overflow (max 10)" */

typedef struct {
    float pos[3];
    int link[4]; /* by mapper direction: lane +1, straight, lane -1, back */
    int chunk, side;
} wp_t;

typedef struct {
    int n;
    int id[OCC_MAX];
    float x[OCC_MAX], z[OCC_MAX], speed[OCC_MAX];
} occ_t;

struct a2_aiw {
    wp_t *wp;
    int wp_count;
    occ_t *occ;
    const a2_track *tr;
    int chunk_count;
};

a2_aiw *a2_aiw_create(const a2_track *tr)
{
    if (!tr || !tr->waypoint_count)
        return NULL;
    a2_aiw *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->wp_count = (int)tr->waypoint_count;
    w->wp = calloc((size_t)w->wp_count, sizeof *w->wp);
    w->occ = calloc((size_t)w->wp_count, sizeof *w->occ);
    if (!w->wp || !w->occ) {
        a2_aiw_free(w);
        return NULL;
    }
    w->tr = tr;
    w->chunk_count = (int)tr->pmp.chunk_count;
    for (int i = 0; i < w->wp_count; i++) {
        const a2_waypoint *s = &tr->waypoints[i];
        wp_t *d = &w->wp[i];
        d->pos[0] = s->x;
        d->pos[1] = s->y;
        d->pos[2] = s->z;
        /* runtime +0x18 = L2, +0x14 = L0, +0x1c = L1, +0x20 = L3, and the
         * mapper's directions 0..3 read +0x18, +0x14, +0x1c, +0x20 */
        d->link[A2_AIW_LANE_UP] = s->links[2];
        d->link[A2_AIW_STRAIGHT] = s->links[0];
        d->link[A2_AIW_LANE_DOWN] = s->links[1];
        d->link[A2_AIW_BACK] = s->links[3];
        for (int k = 0; k < 4; k++)
            if (d->link[k] >= w->wp_count)
                d->link[k] = -1;
        d->chunk = -1;
        d->side = 1;
    }
    for (int c = 0; c < w->chunk_count; c++) {
        const a2_pmp_chunk *pc = &tr->pmp.chunks[c];
        for (uint32_t k = 0; k < pc->wp_left_count; k++)
            if ((int)pc->wp_left[k] < w->wp_count) {
                w->wp[pc->wp_left[k]].chunk = c;
                w->wp[pc->wp_left[k]].side = 0;
            }
        for (uint32_t k = 0; k < pc->wp_right_count; k++)
            if ((int)pc->wp_right[k] < w->wp_count) {
                w->wp[pc->wp_right[k]].chunk = c;
                w->wp[pc->wp_right[k]].side = 1;
            }
    }
    return w;
}

void a2_aiw_free(a2_aiw *w)
{
    if (!w)
        return;
    free(w->wp);
    free(w->occ);
    free(w);
}

int a2_aiw_chunk_count(const a2_aiw *w) { return w ? w->chunk_count : 0; }

int a2_aiw_wp(const a2_aiw *w, int wp, float pos[3], int *chunk, int *side)
{
    if (!w || wp < 0 || wp >= w->wp_count)
        return 0;
    if (pos)
        memcpy(pos, w->wp[wp].pos, sizeof w->wp[wp].pos);
    if (chunk)
        *chunk = w->wp[wp].chunk;
    if (side)
        *side = w->wp[wp].side;
    return 1;
}

int a2_aiw_link(const a2_aiw *w, int wp, int dir)
{
    if (!w || wp < 0 || wp >= w->wp_count || dir < 0 || dir > 3)
        return -1;
    return w->wp[wp].link[dir];
}

static int nearest_in(const a2_aiw *w, const uint32_t *list, uint32_t n, float x, float y,
                      float z, float *best)
{
    int bi = -1;
    for (uint32_t k = 0; k < n; k++) {
        int i = (int)list[k];
        if (i < 0 || i >= w->wp_count)
            continue;
        float dx = w->wp[i].pos[0] - x, dy = w->wp[i].pos[1] - y, dz = w->wp[i].pos[2] - z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < *best) {
            *best = d;
            bi = i;
        }
    }
    return bi;
}

int a2_aiw_nearest(const a2_aiw *w, int chunk, float x, float y, float z, int side)
{
    if (!w || chunk < 0 || chunk >= w->chunk_count)
        return -1;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    float best = 3.4e38f;
    int bi = -1, i;
    if (side == 1 || side == -1)
        if ((i = nearest_in(w, pc->wp_right, pc->wp_right_count, x, y, z, &best)) >= 0)
            bi = i;
    if (side == 0 || side == -1)
        if ((i = nearest_in(w, pc->wp_left, pc->wp_left_count, x, y, z, &best)) >= 0)
            bi = i;
    return bi;
}

int a2_aiw_lane_index(const a2_aiw *w, int chunk, int wp)
{
    if (!w || chunk < 0 || chunk >= w->chunk_count)
        return -1;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    for (uint32_t k = 0; k < pc->wp_left_count; k++)
        if ((int)pc->wp_left[k] == wp)
            return (int)k;
    for (uint32_t k = 0; k < pc->wp_right_count; k++)
        if ((int)pc->wp_right[k] == wp)
            return (int)k;
    return -1;
}

int a2_aiw_side_count(const a2_aiw *w, int chunk, int side)
{
    if (!w || chunk < 0 || chunk >= w->chunk_count)
        return 0;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    return (int)(side ? pc->wp_right_count : pc->wp_left_count);
}

int a2_aiw_side_wp(const a2_aiw *w, int chunk, int lane, int side)
{
    int n = a2_aiw_side_count(w, chunk, side);
    if (n <= 0)
        return -1;
    if (lane < 0)
        lane = 0;
    if (lane >= n)
        lane = n - 1;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    return (int)(side ? pc->wp_right[lane] : pc->wp_left[lane]);
}

int a2_aiw_trigger(const a2_aiw *w, int chunk, int slot)
{
    if (!w || chunk < 0 || chunk >= w->chunk_count)
        return 0;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    return (int)(slot ? pc->trigger1 : pc->trigger0);
}

int a2_aiw_chunk_dist(const a2_aiw *w, int a, int b)
{
    int d = abs(a - b);
    int n = w ? w->chunk_count : 0;
    if (d > n / 2)
        return n - d;
    return d;
}

int a2_aiw_ahead(const a2_aiw *w, int a, int b)
{
    int n = w ? w->chunk_count : 0;
    if (a > b && a - b < n / 2)
        return 1;
    if (a < b && b - a > n / 2)
        return 1;
    return 0;
}

int a2_aiw_road_dir(const a2_aiw *w, int chunk, int side, float *dx, float *dz)
{
    if (!w || chunk < 0 || chunk >= w->chunk_count)
        return 0;
    const a2_pmp_chunk *pc = &w->tr->pmp.chunks[chunk];
    const uint32_t *list = side ? pc->wp_right : pc->wp_left;
    uint32_t n = side ? pc->wp_right_count : pc->wp_left_count;
    if (!n || (int)list[0] >= w->wp_count)
        return 0;
    int a = (int)list[0];
    int b = w->wp[a].link[A2_AIW_STRAIGHT];
    if (b < 0)
        return 0;
    *dx = w->wp[b].pos[0] - w->wp[a].pos[0];
    *dz = w->wp[b].pos[2] - w->wp[a].pos[2];
    return 1;
}

int a2_aiw_with_lane(const a2_aiw *w, int wp, const a2_drive *d)
{
    if (!w || wp < 0 || wp >= w->wp_count)
        return 1;
    const wp_t *p = &w->wp[wp];
    /* 0x40a100 tries +0x14, +0x1c, +0x18 and only then the back link +0x20,
     * whose bearing it turns round */
    int flip = 0, next = p->link[A2_AIW_STRAIGHT];
    if (next < 0)
        next = p->link[A2_AIW_LANE_DOWN];
    if (next < 0)
        next = p->link[A2_AIW_LANE_UP];
    if (next < 0) {
        next = p->link[A2_AIW_BACK];
        if (next < 0)
            return 1;
        flip = 1;
    }
    int b = a2_ai_bearing(d, w->wp[next].pos[0], w->wp[next].pos[2]);
    if (flip) {
        b -= 0x800;
        if (b < 0)
            b += 0x1000;
    }
    return (b >= 0x400 && b <= 0xc00) ? -1 : 1;
}

/* --------------------------------------------------------- obstacle lists */

void a2_aiw_occ_clear(a2_aiw *w)
{
    if (w)
        memset(w->occ, 0, sizeof *w->occ * (size_t)w->wp_count);
}

void a2_aiw_occ_add(a2_aiw *w, int wp, int id, float x, float z, float speed)
{
    if (!w || wp < 0 || wp >= w->wp_count)
        return;
    occ_t *o = &w->occ[wp];
    if (o->n == OCC_MAX)
        return;
    o->id[o->n] = id;
    o->x[o->n] = x;
    o->z[o->n] = z;
    o->speed[o->n] = speed;
    o->n++;
}

int a2_aiw_occ_count(const a2_aiw *w, int wp)
{
    if (!w || wp < 0 || wp >= w->wp_count)
        return 0;
    return w->occ[wp].n;
}

int a2_aiw_blocked(const a2_aiw *w, int wp, int self_id, const a2_drive *d)
{
    if (!w || wp < 0 || wp >= w->wp_count)
        return 0;
    const occ_t *o = &w->occ[wp];
    for (int k = 0; k < o->n; k++) {
        if (o->id[k] == self_id)
            continue;
        int b = a2_ai_bearing(d, o->x[k], o->z[k]);
        if ((b < 0x400 || b > 0xc00) && d->v_fwd > o->speed[k])
            return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- ScanPath */

int a2_aiw_scan_path(const a2_aiw *w, int wp, const int order[3], float near[3], float far[3])
{
    enum { DEPTH = 5 };
    struct {
        unsigned char tried[3];
        int wp;
    } e[DEPTH + 2];
    memset(e, 0, sizeof e);
    for (int k = 0; k < DEPTH + 2; k++)
        e[k].wp = -1;
    if (!w || wp < 0 || wp >= w->wp_count)
        return -1;
    int k = 0, cur = wp;
    while (k < DEPTH) {
        e[k].wp = cur;
        int nx = -1;
        if (!e[k].tried[0]) {
            nx = a2_aiw_link(w, cur, order[0]);
            e[k].tried[0] = 1;
        }
        if (nx >= 0 && a2_aiw_occ_count(w, nx))
            nx = -1;
        if (nx >= 0) {
            cur = nx;
            k++;
            memset(e[k].tried, 0, 3);
            continue;
        }
        /* the other two directions, backtracking when both fail */
        for (;;) {
            int found = -1;
            for (int t = 1; t < 3 && found < 0; t++) {
                if (e[k].tried[t])
                    continue;
                int n = a2_aiw_link(w, cur, order[t]);
                e[k].tried[t] = 1;
                if (n >= 0 && !a2_aiw_occ_count(w, n))
                    found = n;
            }
            if (found >= 0) {
                k++;
                memset(e[k].tried, 0, 3);
                cur = found;
                e[k].wp = cur;
                break;
            }
            if (k == 0)
                goto fallback;
            k--;
            cur = e[k].wp;
        }
    }
    if (e[1].wp < 0 || e[2].wp < 0 || e[4].wp < 0)
        return -1;
    memcpy(near, w->wp[e[2].wp].pos, sizeof(float) * 3);
    memcpy(far, w->wp[e[4].wp].pos, sizeof(float) * 3);
    return e[1].wp;

fallback:
    /* 0x409a2c: every path is blocked, so take the first links that exist,
     * occupied or not, two deep.  The original then rotates `near` by a step
     * that reads a chunk record at an index made from a float (0x409ae9); the
     * port leaves that out. */
    {
        int a = -1;
        for (int t = 0; t < 3 && a < 0; t++)
            a = a2_aiw_link(w, wp, order[t]);
        if (a < 0) {
            memcpy(near, w->wp[wp].pos, sizeof(float) * 3);
            return -1;
        }
        int b = -1;
        for (int t = 0; t < 3 && b < 0; t++)
            b = a2_aiw_link(w, a, order[t]);
        if (b <= 0) {
            memcpy(near, w->wp[a].pos, sizeof(float) * 3);
            return -1;
        }
        memcpy(near, w->wp[b].pos, sizeof(float) * 3);
        return b;
    }
}
