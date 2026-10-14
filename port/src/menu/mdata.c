#include "mdata.h"

#include "../a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *d;
    size_t size;
    uint32_t data_va, data_off, data_size; /* .data */
} pe;

static int pe_open(pe *p, const uint8_t *d, size_t size)
{
    p->d = d;
    p->size = size;
    if (size < 0x200 || d[0] != 'M' || d[1] != 'Z')
        return -1;
    uint32_t h = d[0x3c] | d[0x3d] << 8 | d[0x3e] << 16 | (uint32_t)d[0x3f] << 24;
    if (h + 0x100 > size || memcmp(d + h, "PE\0\0", 4) != 0)
        return -1;
    uint32_t stamp = d[h + 8] | d[h + 9] << 8 | d[h + 10] << 16 | (uint32_t)d[h + 11] << 24;
    if (stamp != 0x363DD150u) {
        fprintf(stderr, "menu: menu.dat timestamp %#x is not v1.01 (0x363dd150)\n", stamp);
        return -1;
    }
    int ns = d[h + 6] | d[h + 7] << 8;
    int osz = d[h + 20] | d[h + 21] << 8;
    for (int i = 0; i < ns; i++) {
        const uint8_t *s = d + h + 24 + osz + 40 * i;
        if (memcmp(s, ".data", 5) == 0) {
            uint32_t vsize = s[8] | s[9] << 8 | s[10] << 16 | (uint32_t)s[11] << 24;
            uint32_t va = s[12] | s[13] << 8 | s[14] << 16 | (uint32_t)s[15] << 24;
            uint32_t rsize = s[16] | s[17] << 8 | s[18] << 16 | (uint32_t)s[19] << 24;
            uint32_t roff = s[20] | s[21] << 8 | s[22] << 16 | (uint32_t)s[23] << 24;
            p->data_va = 0x400000u + va;
            p->data_off = roff;
            p->data_size = rsize < vsize ? rsize : vsize;
            return 0;
        }
    }
    return -1;
}

static const uint8_t *at(const pe *p, uint32_t va, uint32_t len)
{
    if (va < p->data_va || va + len > p->data_va + p->data_size)
        return NULL;
    return p->d + p->data_off + (va - p->data_va);
}

static int32_t i32(const pe *p, uint32_t va)
{
    const uint8_t *b = at(p, va, 4);
    return b ? (int32_t)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24) : -1;
}

static void str(const pe *p, uint32_t va, char *out, size_t cap)
{
    out[0] = '\0';
    const uint8_t *b = at(p, va, 1);
    if (!b)
        return;
    size_t i = 0;
    while (i + 1 < cap && va + i < p->data_va + p->data_size && b[i])
        out[i] = (char)b[i], i++;
    out[i] = '\0';
}

static void load_form(const pe *p, uint32_t va, m_form *f)
{
    f->count = 0;
    for (; f->count < M_MAX_CONTROLS; va += 28) {
        int32_t x = i32(p, va), y = i32(p, va + 4), type = i32(p, va + 8);
        uint32_t desc = (uint32_t)i32(p, va + 12);
        if (x == -1 && type == -1)
            break;
        m_control *c = &f->c[f->count++];
        memset(c, 0, sizeof *c);
        c->x = x;
        c->y = y;
        c->type = type;
        c->state = i32(p, va + 16);
        if (type == 1) {
            const uint8_t *b = at(p, desc, 17);
            if (b) {
                c->id = i32(p, desc);
                c->hotkey = (char)b[12];
                c->style = (uint32_t)i32(p, desc + 21);
                str(p, (uint32_t)i32(p, desc + 13), c->label, sizeof c->label);
            }
        } else if (type == 4) {
            c->w = i32(p, desc);
            c->h = i32(p, desc + 4);
        }
    }
}

int m_data_load(m_data *d, const char *datadir)
{
    memset(d, 0, sizeof *d);
    char path[1024];
    snprintf(path, sizeof path, "%s/menu.dat", datadir);
    size_t size = 0;
    uint8_t *bytes = a2_read_file(path, &size);
    if (!bytes) {
        fprintf(stderr, "menu: cannot read %s\n", path);
        return -1;
    }
    pe p;
    if (pe_open(&p, bytes, size) != 0) {
        free(bytes);
        return -1;
    }
    static const uint32_t forms[M_FORM_COUNT] = {0x42ed40, 0x42a238, 0x42b628, 0x42c2f8,
                                                 0x42da50, 0x430080, 0x430f60};
    for (int i = 0; i < M_FORM_COUNT; i++)
        load_form(&p, forms[i], &d->form[i]);

    /* animations, 0x433e60 (registered by 0x413750 from 0x409e15) */
    for (int k = 0; k < 64; k++) {
        uint32_t va = 0x433e60 + 12u * (uint32_t)k;
        int32_t n = i32(&p, va);
        uint32_t list = (uint32_t)i32(&p, va + 8);
        if (n < 0)
            break;
        m_anim *an = &d->anim[k];
        an->frames = n > M_MAX_FRAMES ? M_MAX_FRAMES : n;
        for (int f = 0; f < an->frames; f++)
            an->sprite[f] = i32(&p, list + 8u * (uint32_t)f + 4) - 1;
        d->anim_count = k + 1;
    }
    /* cars, 0x42df54: {anim, name[40], index}, 48 bytes each (mm_open_screen
     * reads the anim at 0x42df54 + 48 * car) */
    for (int k = 0; k < M_CARS; k++) {
        uint32_t va = 0x42df54 + 48u * (uint32_t)k;
        d->car[k].anim = i32(&p, va);
        str(&p, va + 4, d->car[k].name, sizeof d->car[k].name);
    }
    free(bytes);
    if (d->anim_count < 35 || strcmp(d->car[0].name, "Brabant") != 0 ||
        d->form[M_FORM_MAIN].count < 8) {
        fprintf(stderr, "menu: menu.dat tables not where expected\n");
        return -1;
    }
    return 0;
}
