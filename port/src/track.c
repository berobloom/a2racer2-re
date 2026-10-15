#include "track.h"

#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's own rd_chunk.cpp limits; used as sanity bounds while parsing. */
static const uint32_t rmp_list_max[A2_RMP_LISTS] = {16, 24, 12, 64};

static char *join_path(const char *dir, const char *stem, const char *ext)
{
    size_t n = strlen(dir) + 1 + strlen(stem) + strlen(ext) + 1;
    char *p = malloc(n);
    if (p)
        snprintf(p, n, "%s/%s%s", dir, stem, ext);
    return p;
}

static uint8_t *load(const char *dir, const char *stem, const char *ext, size_t *size)
{
    char *path = join_path(dir, stem, ext);
    if (!path)
        return NULL;
    uint8_t *data = a2_read_file(path, size);
    if (!data)
        fprintf(stderr, "track: cannot read %s\n", path);
    free(path);
    return data;
}

/* ------------------------------------------------------------------ .rmp */

static int parse_rmp(a2_rmp *out, const uint8_t *data, size_t size)
{
    memset(out, 0, sizeof(*out));
    a2_cur c;
    a2_cur_init(&c, data, size);
    out->plane_count_hint = a2_u32(&c);
    uint32_t n = a2_u32(&c);
    if (!a2_cur_ok(&c) || n > 100000) {
        fprintf(stderr, "rmp: implausible chunk count %u\n", n);
        return -1;
    }
    out->chunks = calloc(n ? n : 1, sizeof(a2_rmp_chunk));
    if (!out->chunks)
        return -1;
    out->chunk_count = n;

    for (uint32_t i = 0; i < n; i++) {
        a2_rmp_chunk *ch = &out->chunks[i];
        for (int l = 0; l < A2_RMP_LISTS; l++) {
            uint32_t cnt = a2_u32(&c);
            if (!a2_cur_ok(&c) || cnt > rmp_list_max[l]) {
                fprintf(stderr, "rmp: chunk %u list %d has implausible count %u\n", i, l, cnt);
                return -1;
            }
            ch->counts[l] = cnt;
            if (!cnt)
                continue;
            ch->planes[l] = calloc(cnt, sizeof(a2_plane));
            if (!ch->planes[l])
                return -1;
            for (uint32_t p = 0; p < cnt; p++) {
                a2_plane *pl = &ch->planes[l][p];
                for (int k = 0; k < 4; k++) {
                    pl->v[k].x = a2_f32(&c);
                    pl->v[k].y = a2_f32(&c);
                    pl->v[k].z = a2_f32(&c);
                    pl->v[k].u = a2_f32(&c);
                    pl->v[k].v = a2_f32(&c);
                }
                for (int t = 0; t < 2; t++)
                    for (int k = 0; k < 3; k++)
                        pl->tri[t][k] = a2_u32(&c);
            }
        }
    }
    if (!a2_cur_ok(&c)) {
        fprintf(stderr, "rmp: truncated\n");
        return -1;
    }
    if (a2_cur_left(&c) != 0)
        fprintf(stderr, "rmp: warning, %zu trailing bytes\n", a2_cur_left(&c));
    return 0;
}

static void free_rmp(a2_rmp *r)
{
    for (uint32_t i = 0; i < r->chunk_count; i++)
        for (int l = 0; l < A2_RMP_LISTS; l++)
            free(r->chunks[i].planes[l]);
    free(r->chunks);
    memset(r, 0, sizeof(*r));
}

/* ------------------------------------------------------------------ .pmp */

static int parse_pmp(a2_pmp *out, const uint8_t *data, size_t size)
{
    memset(out, 0, sizeof(*out));
    a2_cur c;
    a2_cur_init(&c, data, size);
    uint32_t n = a2_u32(&c);
    if (!a2_cur_ok(&c) || n > 100000) {
        fprintf(stderr, "pmp: implausible chunk count %u\n", n);
        return -1;
    }
    out->chunks = calloc(n ? n : 1, sizeof(a2_pmp_chunk));
    if (!out->chunks)
        return -1;
    out->chunk_count = n;

    for (uint32_t i = 0; i < n; i++) {
        a2_pmp_chunk *ch = &out->chunks[i];
        ch->x = a2_f32(&c);
        ch->y = a2_f32(&c);
        ch->z = a2_f32(&c);
        ch->trigger0 = a2_u32(&c);
        ch->trigger1 = a2_u32(&c);

        uint32_t nr = a2_u32(&c);
        if (!a2_cur_ok(&c) || nr > 4096) {
            fprintf(stderr, "pmp: chunk %u rail count %u\n", i, nr);
            return -1;
        }
        if (nr) {
            ch->rails = calloc(nr, sizeof(uint32_t));
            if (!ch->rails)
                return -1;
            for (uint32_t k = 0; k < nr; k++)
                ch->rails[k] = a2_u32(&c);
        }
        ch->rail_count = nr;

        uint32_t *dst[2] = {ch->wp_left, ch->wp_right};
        uint32_t *dstn[2] = {&ch->wp_left_count, &ch->wp_right_count};
        for (int s = 0; s < 2; s++) {
            uint32_t cnt = a2_u32(&c);
            /* rd_chunk.cpp rejects >12 with "chunk waypoint left overflow". */
            if (!a2_cur_ok(&c) || cnt > A2_CHUNK_WP_MAX) {
                fprintf(stderr, "pmp: chunk %u waypoint count %u exceeds %d\n", i, cnt,
                        A2_CHUNK_WP_MAX);
                return -1;
            }
            for (uint32_t k = 0; k < cnt; k++)
                dst[s][k] = a2_u32(&c);
            *dstn[s] = cnt;
        }
    }
    if (!a2_cur_ok(&c)) {
        fprintf(stderr, "pmp: truncated\n");
        return -1;
    }
    if (a2_cur_left(&c) != 0)
        fprintf(stderr, "pmp: warning, %zu trailing bytes\n", a2_cur_left(&c));
    return 0;
}

static void free_pmp(a2_pmp *p)
{
    for (uint32_t i = 0; i < p->chunk_count; i++)
        free(p->chunks[i].rails);
    free(p->chunks);
    memset(p, 0, sizeof(*p));
}

/* ------------------------------------------------- .cmp / .dmp / .omp */

static int parse_cmp(a2_track *tr, const uint8_t *data, size_t size)
{
    a2_cur c;
    a2_cur_init(&c, data, size);
    uint32_t n = a2_u32(&c);
    if (4 + (size_t)n * 24 != size) {
        fprintf(stderr, "cmp: count %u does not fit %zu bytes\n", n, size);
        return -1;
    }
    tr->rails = calloc(n ? n : 1, sizeof(a2_rail));
    if (!tr->rails)
        return -1;
    for (uint32_t i = 0; i < n; i++) {
        a2_rail *r = &tr->rails[i];
        r->x = a2_f32(&c);
        r->y = a2_f32(&c);
        r->z = a2_f32(&c);
        r->dx = a2_f32(&c);
        r->dy = a2_f32(&c);
        r->dz = a2_f32(&c);
    }
    tr->rail_count = n;
    return a2_cur_ok(&c) ? 0 : -1;
}

static int parse_dmp(a2_track *tr, const uint8_t *data, size_t size)
{
    a2_cur c;
    a2_cur_init(&c, data, size);
    uint32_t n = a2_u32(&c);
    if (4 + (size_t)n * 36 != size) {
        fprintf(stderr, "dmp: count %u does not fit %zu bytes\n", n, size);
        return -1;
    }
    tr->waypoints = calloc(n ? n : 1, sizeof(a2_waypoint));
    if (!tr->waypoints)
        return -1;
    for (uint32_t i = 0; i < n; i++) {
        a2_waypoint *w = &tr->waypoints[i];
        w->type = a2_u32(&c);
        w->flag = a2_u32(&c); /* 2 at the pumps (16 waypoints, 2 per station) */
        w->x = a2_f32(&c);
        w->y = a2_f32(&c);
        w->z = a2_f32(&c);
        for (int k = 0; k < 4; k++) {
            uint32_t v = a2_u32(&c);
            w->links[k] = (v == 0xFFFFFFFFu) ? -1 : (int32_t)v;
        }
    }
    tr->waypoint_count = n;
    return a2_cur_ok(&c) ? 0 : -1;
}

static int parse_omp(a2_track *tr, const uint8_t *data, size_t size)
{
    a2_cur c;
    a2_cur_init(&c, data, size);
    uint32_t n = a2_u32(&c);
    /* etappe2.omp and etappe6.omp declare one record more than they contain. */
    uint32_t avail = (uint32_t)((size - 4) / 68);
    if (n > avail) {
        fprintf(stderr, "omp: declares %u records but only %u fit; clamping\n", n, avail);
        n = avail;
    }
    tr->objects = calloc(n ? n : 1, sizeof(a2_object));
    if (!tr->objects)
        return -1;
    for (uint32_t i = 0; i < n; i++) {
        a2_object *o = &tr->objects[i];
        o->chunk = a2_i32(&c);
        o->type = a2_i32(&c);
        o->x = a2_f32(&c);
        o->y = a2_f32(&c);
        o->z = a2_f32(&c);
        o->rot_x = a2_f32(&c);
        o->rot_y = a2_f32(&c);
        o->rot_z = a2_f32(&c);
        uint8_t raw[A2_OMP_NAME_LEN];
        a2_bytes(&c, raw, sizeof raw);
        size_t k = 0;
        while (k < sizeof raw && raw[k])
            k++;
        memcpy(o->name, raw, k);
        o->name[k] = '\0';
    }
    tr->object_count = n;
    return a2_cur_ok(&c) ? 0 : -1;
}

/* ------------------------------------------------------------------ api */

/* Camera paths.  The original's loader (fcn.00433710, pth_main.cpp) rejects any
 * version other than -1, so we do the same. */
static int parse_pth(a2_track *tr, const uint8_t *data, size_t size)
{
    a2_cur c;
    a2_cur_init(&c, data, size);
    tr->path_version = a2_i32(&c);
    if (tr->path_version != -1) {
        fprintf(stderr, "pth: version %d, expected -1\n", tr->path_version);
        return -1;
    }
    uint32_t n = a2_u32(&c);
    if (!a2_cur_ok(&c))
        return -1;
    tr->paths = calloc(n ? n : 1, sizeof(a2_path));
    if (!tr->paths)
        return -1;
    for (uint32_t i = 0; i < n; i++) {
        a2_path *p = &tr->paths[i];
        p->first_chunk = a2_u32(&c);
        p->last_chunk = a2_u32(&c);
        p->f2 = a2_u32(&c);
        p->f3 = a2_u32(&c);
        p->f4 = a2_u32(&c);
        uint32_t np = a2_u32(&c);
        if (!a2_cur_ok(&c))
            return -1;
        p->points = calloc(np ? np : 1, sizeof(a2_path_point));
        if (!p->points)
            return -1;
        for (uint32_t k = 0; k < np; k++) {
            p->points[k].flag = a2_u32(&c);
            p->points[k].x = a2_f32(&c);
            p->points[k].y = a2_f32(&c);
            p->points[k].z = a2_f32(&c);
        }
        p->point_count = np;
        tr->path_count = i + 1;
    }
    return a2_cur_ok(&c) ? 0 : -1;
}

int a2_track_load(a2_track *tr, const char *dir, const char *stem)
{
    memset(tr, 0, sizeof(*tr));
    size_t size = 0;
    uint8_t *data;
    int rc = -1;

    data = load(dir, stem, ".rmp", &size);
    if (!data)
        goto done;
    rc = parse_rmp(&tr->rmp, data, size);
    free(data);
    if (rc != 0)
        goto done;

    data = load(dir, stem, ".pmp", &size);
    if (!data) {
        rc = -1;
        goto done;
    }
    rc = parse_pmp(&tr->pmp, data, size);
    free(data);
    if (rc != 0)
        goto done;

    if (tr->rmp.chunk_count != tr->pmp.chunk_count) {
        /* The original prints "Data-chunk count does not match draw-chunk count". */
        fprintf(stderr, "track: rmp has %u chunks but pmp has %u\n", tr->rmp.chunk_count,
                tr->pmp.chunk_count);
        rc = -1;
        goto done;
    }

    /* The remaining three are not needed to draw the road, so a failure here
     * is reported but does not sink the load. */
    if ((data = load(dir, stem, ".cmp", &size)) != NULL) {
        parse_cmp(tr, data, size);
        free(data);
    }
    if ((data = load(dir, stem, ".dmp", &size)) != NULL) {
        parse_dmp(tr, data, size);
        free(data);
    }
    if ((data = load(dir, stem, ".omp", &size)) != NULL) {
        parse_omp(tr, data, size);
        free(data);
    }
    if ((data = load(dir, stem, ".pth", &size)) != NULL) {
        parse_pth(tr, data, size);
        free(data);
    }
    rc = 0;

done:
    if (rc != 0)
        a2_track_free(tr);
    return rc;
}

void a2_track_free(a2_track *tr)
{
    free_rmp(&tr->rmp);
    free_pmp(&tr->pmp);
    free(tr->rails);
    free(tr->waypoints);
    free(tr->objects);
    for (uint32_t i = 0; i < tr->path_count; i++)
        free(tr->paths[i].points);
    free(tr->paths);
    memset(tr, 0, sizeof(*tr));
}
