#include "anims.h"

#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Addresses in the retail spel.dat (image base 0x400000). */
#define VA_ANIM_RECORDS 0x492050
#define VA_ANIM_NAMES 0x490430
#define VA_FRAMES 0x48E5E8
#define VA_MESH_NAMES 0x48D0DC
#define VA_TEXTURE_NAMES 0x48E19C

#define N_ANIMS 312
#define N_FRAMES 386
#define N_MESH_NAMES 357
#define N_TEXTURE_NAMES 91

#define ANIM_STRIDE 9
#define FRAME_STRIDE 16
#define NAME_STRIDE 12

struct a2_anims {
    uint8_t *image; /* the whole spel.dat, owned; all names point into it */
    size_t size;
    char **anim_names;               /* N_ANIMS */
    a2_anim_frame *frames;           /* flattened, one run per anim */
    int *frame_first;                /* N_ANIMS */
    int *frame_count;                /* N_ANIMS */
    int total_frames;
};

/* Map a virtual address to a file offset using the PE section table. */
typedef struct {
    uint32_t va, vsize, raw, rsize;
} section;

static int pe_sections(const uint8_t *d, size_t n, section *out, int max, uint32_t *base, int *count)
{
    if (n < 0x40)
        return -1;
    uint32_t pe = (uint32_t)(d[0x3c] | (d[0x3d] << 8) | (d[0x3e] << 16) | ((uint32_t)d[0x3f] << 24));
    if (pe + 24 > n || memcmp(d + pe, "PE\0\0", 4) != 0)
        return -1;
    int nsec = d[pe + 6] | (d[pe + 7] << 8);
    int opt = d[pe + 20] | (d[pe + 21] << 8);
    if (pe + 24 + (size_t)opt + (size_t)nsec * 40 > n || nsec > max)
        return -1;
    memcpy(base, d + pe + 0x34, 4);
    for (int i = 0; i < nsec; i++) {
        const uint8_t *e = d + pe + 24 + opt + (size_t)i * 40;
        memcpy(&out[i].vsize, e + 8, 4);
        memcpy(&out[i].va, e + 12, 4);
        memcpy(&out[i].rsize, e + 16, 4);
        memcpy(&out[i].raw, e + 20, 4);
    }
    *count = nsec;
    return 0;
}

typedef struct {
    const uint8_t *d;
    size_t n;
    section sec[16];
    int nsec;
    uint32_t base;
} pe_map;

static long va_to_off(const pe_map *m, uint32_t va)
{
    for (int i = 0; i < m->nsec; i++) {
        uint32_t lo = m->base + m->sec[i].va;
        if (va >= lo && va < lo + m->sec[i].rsize) {
            long off = (long)m->sec[i].raw + (long)(va - lo);
            return (off >= 0 && (size_t)off < m->n) ? off : -1;
        }
    }
    return -1;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Borrow a NUL-terminated ASCII string living at `va`, or NULL. */
static char *str_at(const pe_map *m, uint32_t va)
{
    long o = va_to_off(m, va);
    if (o < 0)
        return NULL;
    size_t i = (size_t)o;
    while (i < m->n && m->d[i]) {
        if (m->d[i] < 32 || m->d[i] > 126)
            return NULL;
        if (i - (size_t)o > 48)
            return NULL;
        i++;
    }
    return (i < m->n && i > (size_t)o) ? (char *)m->d + o : NULL;
}

/* Read `count` name pointers out of an array of 12-byte {u32, char*, u32} records. */
static char **read_names(const pe_map *m, uint32_t va, int count)
{
    char **out = calloc(count ? count : 1, sizeof(char *));
    if (!out)
        return NULL;
    for (int i = 0; i < count; i++) {
        long o = va_to_off(m, va + (uint32_t)i * NAME_STRIDE);
        if (o < 0 || (size_t)o + NAME_STRIDE > m->n) {
            free(out);
            return NULL;
        }
        out[i] = str_at(m, rd32(m->d + o + 4));
    }
    return out;
}

a2_anims *a2_anims_load(const char *datadir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/spel.dat", datadir);
    size_t size = 0;
    uint8_t *image = a2_read_file(path, &size);
    if (!image) {
        fprintf(stderr, "anims: cannot read %s\n", path);
        return NULL;
    }

    pe_map m;
    m.d = image;
    m.n = size;
    if (pe_sections(image, size, m.sec, 16, &m.base, &m.nsec) != 0) {
        fprintf(stderr, "anims: %s is not a PE image\n", path);
        free(image);
        return NULL;
    }

    a2_anims *a = calloc(1, sizeof(*a));
    if (!a) {
        free(image);
        return NULL;
    }
    a->image = image;
    a->size = size;

    char **mesh_names = read_names(&m, VA_MESH_NAMES, N_MESH_NAMES);
    char **tex_names = read_names(&m, VA_TEXTURE_NAMES, N_TEXTURE_NAMES);
    a->anim_names = calloc(N_ANIMS, sizeof(char *));
    if (!mesh_names || !tex_names || !a->anim_names)
        goto fail;

    for (int i = 0; i < N_ANIMS; i++) {
        long o = va_to_off(&m, VA_ANIM_NAMES + (uint32_t)i * 4);
        if (o < 0)
            goto fail;
        a->anim_names[i] = str_at(&m, rd32(m.d + o));
    }
    /* Sanity: the table should look like the one we reverse engineered. */
    if (!a->anim_names[0] || strcasecmp(a->anim_names[0], "OBJ_LIGHT01_ANIM") != 0 ||
        !a->anim_names[N_ANIMS - 1] ||
        strcasecmp(a->anim_names[N_ANIMS - 1], "E05_ALGEMENE_TEXTURES_ANIM") != 0) {
        fprintf(stderr, "anims: %s does not match the expected build; skipping textures\n", path);
        goto fail;
    }

    a->frame_first = calloc(N_ANIMS, sizeof(int));
    a->frame_count = calloc(N_ANIMS, sizeof(int));
    if (!a->frame_first || !a->frame_count)
        goto fail;

    /* Pass 1: total frames.  Pass 2: fill.  Anims whose frame pointer is not
     * file-backed (OBJ_LIGHT01_ANIM) are simply left empty. */
    int total = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            a->frames = calloc(total ? total : 1, sizeof(a2_anim_frame));
            if (!a->frames)
                goto fail;
            total = 0;
        }
        for (int i = 0; i < N_ANIMS; i++) {
            long o = va_to_off(&m, VA_ANIM_RECORDS + (uint32_t)i * ANIM_STRIDE);
            if (o < 0 || (size_t)o + ANIM_STRIDE > m.n)
                goto fail;
            int cnt = (int)rd32(m.d + o);
            uint32_t fptr = rd32(m.d + o + 4);
            long fo = va_to_off(&m, fptr);
            if (cnt <= 0 || cnt > 256 || fo < 0) {
                if (pass == 1) {
                    a->frame_first[i] = total;
                    a->frame_count[i] = 0;
                }
                continue;
            }
            if (pass == 0) {
                total += cnt;
                continue;
            }
            a->frame_first[i] = total;
            a->frame_count[i] = cnt;
            for (int k = 0; k < cnt; k++) {
                long io = fo + (long)k * 4;
                int fi = (io >= 0 && (size_t)io + 4 <= m.n) ? (int)rd32(m.d + io) : -1;
                a2_anim_frame *fr = &a->frames[total + k];
                fr->mesh = fr->texture = NULL;
                if (fi < 0 || fi >= N_FRAMES)
                    continue;
                long feo = va_to_off(&m, VA_FRAMES + (uint32_t)fi * FRAME_STRIDE);
                if (feo < 0 || (size_t)feo + FRAME_STRIDE > m.n)
                    continue;
                int mi = (int)rd32(m.d + feo + 8);
                int ti = (int)rd32(m.d + feo + 12);
                if (mi >= 0 && mi < N_MESH_NAMES)
                    fr->mesh = mesh_names[mi];
                if (ti >= 0 && ti < N_TEXTURE_NAMES)
                    fr->texture = tex_names[ti];
            }
            total += cnt;
        }
    }
    a->total_frames = total;

    free(mesh_names);
    free(tex_names);
    return a;

fail:
    free(mesh_names);
    free(tex_names);
    a2_anims_free(a);
    return NULL;
}

void a2_anims_free(a2_anims *a)
{
    if (!a)
        return;
    free(a->anim_names);
    free(a->frames);
    free(a->frame_first);
    free(a->frame_count);
    free(a->image);
    free(a);
}

const a2_anim_frame *a2_anims_find(const a2_anims *a, const char *name, int *out_count)
{
    if (!a || !name)
        return NULL;
    for (int i = 0; i < N_ANIMS; i++) {
        if (a->anim_names[i] && strcasecmp(a->anim_names[i], name) == 0) {
            if (!a->frame_count[i])
                return NULL;
            if (out_count)
                *out_count = a->frame_count[i];
            return &a->frames[a->frame_first[i]];
        }
    }
    return NULL;
}

int a2_anims_count(const a2_anims *a) { return a ? N_ANIMS : 0; }
