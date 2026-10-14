#include "massets.h"

#include "../image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int open_pair(a2_archive *ar, const char *dir, const char *img, const char *ind)
{
    char pi[1024], pd[1024];
    snprintf(pi, sizeof pi, "%s/%s", dir, img);
    snprintf(pd, sizeof pd, "%s/%s", dir, ind);
    return a2_archive_open(ar, pi, pd);
}

const uint8_t *m_file(const m_assets *a, const char *name, uint32_t *size)
{
    long i = a2_archive_find(&a->mrcs, name);
    if (i < 0)
        return NULL;
    return a2_archive_data(&a->mrcs, (size_t)i, size);
}

static int load_font(m_assets *a, int slot, const char *name)
{
    uint32_t size = 0;
    const uint8_t *d = m_file(a, name, &size);
    if (!d || size < 264)
        return -1;
    int w = d[256] | d[257] << 8, h = d[258] | d[259] << 8;
    if (264u + 256u * (uint32_t)w * (uint32_t)h != size)
        return -1;
    a->font[slot].w = w;
    a->font[slot].h = h;
    a->font[slot].ink = d[260] | d[261] << 8;
    a->font[slot].present = d;
    a->font[slot].glyphs = d + 264;
    return 0;
}

int m_assets_open(m_assets *a, const char *datadir)
{
    memset(a, 0, sizeof *a);
    /* The shipped data spells these in mixed case; try the common spellings. */
    if (open_pair(&a->mrcs, datadir, "Mrcs.img", "Mrcs.ind") != 0 &&
        open_pair(&a->mrcs, datadir, "MRCS.IMG", "MRCS.IND") != 0) {
        fprintf(stderr, "menu: cannot open Mrcs.img\n");
        return -1;
    }
    if (open_pair(&a->menurc, datadir, "MENURC.IMG", "MENURC.IND") != 0 &&
        open_pair(&a->menurc, datadir, "menurc.img", "menurc.ind") != 0) {
        fprintf(stderr, "menu: cannot open MENURC.IMG\n");
        return -1;
    }
    static const char *names[M_FONT_COUNT] = {"CARVED.IMG", "SMALLBT.IMG", "ascii12.img",
                                              "cyber.img",  "BUTFNT.IMG",  "STDFONT.IMG",
                                              "midfont.img"};
    for (int i = 0; i < M_FONT_COUNT; i++)
        if (load_font(a, i, names[i]) != 0) {
            fprintf(stderr, "menu: bad font %s\n", names[i]);
            return -1;
        }
    uint32_t size = 0;
    const uint8_t *bg = m_file(a, "hoofd.PCX", &size);
    if (!bg || a2_pcx_palette(a->pal, bg, size) != 0) {
        fprintf(stderr, "menu: no palette (hoofd.PCX)\n");
        return -1;
    }
    return 0;
}

void m_assets_close(m_assets *a)
{
    a2_archive_close(&a->mrcs);
    a2_archive_close(&a->menurc);
}

int m_sprite_count(const m_assets *a) { return (int)a->menurc.count; }

int m_sprite_get(const m_assets *a, int index, m_sprite *out)
{
    if (index < 0 || (size_t)index >= a->menurc.count)
        return -1;
    uint32_t size = 0;
    const uint8_t *d = a2_archive_data(&a->menurc, (size_t)index, &size);
    if (!d || size < 8)
        return -1;
    out->w = d[0] | d[1] << 8;
    out->h = d[2] | d[3] << 8;
    out->ax = d[4] | d[5] << 8;
    out->ay = d[6] | d[7] << 8;
    if (8u + (uint32_t)out->w * (uint32_t)out->h > size)
        return -1;
    out->px = d + 8;
    return 0;
}

uint8_t *m_background(const m_assets *a, const char *name)
{
    uint32_t size = 0;
    const uint8_t *d = m_file(a, name, &size);
    if (!d)
        return NULL;
    int w = 0, h = 0;
    uint8_t *px = a2_pcx_decode_indexed(d, size, name, &w, &h);
    if (px && (w != M_W || h != M_H)) {
        free(px);
        return NULL;
    }
    return px;
}
