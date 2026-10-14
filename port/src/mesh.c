#include "mesh.h"

#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t expected_size(uint32_t v, uint32_t t)
{
    return 8 + (size_t)v * A2_MESH_VERTEX_LEN + (size_t)t * A2_MESH_TRI_LEN;
}

static int header_at(const uint8_t *d, size_t size, size_t base, uint32_t *pv, uint32_t *pt)
{
    if (base + 8 > size)
        return 0;
    uint32_t v = (uint32_t)d[base] | ((uint32_t)d[base + 1] << 8) | ((uint32_t)d[base + 2] << 16) |
                 ((uint32_t)d[base + 3] << 24);
    uint32_t t = (uint32_t)d[base + 4] | ((uint32_t)d[base + 5] << 8) |
                 ((uint32_t)d[base + 6] << 16) | ((uint32_t)d[base + 7] << 24);
    if (base + expected_size(v, t) != size)
        return 0;
    *pv = v;
    *pt = t;
    return 1;
}

int a2_mesh_parse(a2_mesh *m, const uint8_t *data, size_t size, const char *name)
{
    memset(m, 0, sizeof(*m));
    if (!data || size < 8) {
        fprintf(stderr, "mesh %s: too short\n", name ? name : "?");
        return -1;
    }

    uint32_t vcount = 0, tcount = 0;
    size_t base = 0;
    if (!header_at(data, size, 0, &vcount, &tcount)) {
        /* Relocate past stray leading bytes (see header comment). */
        int found = 0;
        for (size_t b = 4; b <= 64 && b + 8 <= size; b += 4) {
            if (header_at(data, size, b, &vcount, &tcount)) {
                base = b;
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "mesh %s: size %zu does not match any header\n", name ? name : "?",
                    size);
            return -1;
        }
    }

    m->vertices = calloc(vcount ? vcount : 1, sizeof(a2_vertex));
    m->triangles = calloc(tcount ? tcount : 1, sizeof(a2_tri));
    if (!m->vertices || !m->triangles) {
        a2_mesh_free(m);
        return -1;
    }

    a2_cur c;
    a2_cur_init(&c, data + base, size - base);
    a2_skip(&c, 8);
    for (uint32_t i = 0; i < vcount; i++) {
        a2_vertex *v = &m->vertices[i];
        v->x = a2_f32(&c);
        v->y = a2_f32(&c);
        v->z = a2_f32(&c);
        v->nx = a2_f32(&c);
        v->ny = a2_f32(&c);
        v->nz = a2_f32(&c);
        v->u = a2_f32(&c);
        v->v = a2_f32(&c);
        a2_skip(&c, 1); /* dead pad */
    }
    for (uint32_t i = 0; i < tcount; i++) {
        m->triangles[i].a = a2_u16(&c);
        m->triangles[i].b = a2_u16(&c);
        m->triangles[i].c = a2_u16(&c);
        a2_skip(&c, 3); /* dead pad */
    }
    if (!a2_cur_ok(&c)) {
        fprintf(stderr, "mesh %s: truncated\n", name ? name : "?");
        a2_mesh_free(m);
        return -1;
    }

    for (uint32_t i = 0; i < tcount; i++) {
        const a2_tri *t = &m->triangles[i];
        if (t->a >= vcount || t->b >= vcount || t->c >= vcount) {
            fprintf(stderr, "mesh %s: triangle %u indexes outside %u vertices\n",
                    name ? name : "?", i, vcount);
            a2_mesh_free(m);
            return -1;
        }
    }

    m->vertex_count = vcount;
    m->triangle_count = tcount;
    return 0;
}

void a2_mesh_free(a2_mesh *m)
{
    free(m->vertices);
    free(m->triangles);
    memset(m, 0, sizeof(*m));
}
