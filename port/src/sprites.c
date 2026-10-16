#include "sprites.h"

#include "gfx.h"
#include "image.h"

#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPRITE_TRANSPARENT 0xFF

typedef struct {
    int w, h;
    int ax, ay; /* the anchor the blit 0x468db0 subtracts unless flag 2 is set */
    unsigned tex;
} sprite_entry;

struct a2_sprites {
    a2_archive archive;
    sprite_entry *entries;
    int count;
    uint8_t palette[768];
};

/* Decode one entry and upload it as a GL texture.  All 453 are uploaded up
 * front: the whole set is under 3 MB, so it is not worth a lazy path. */
static int upload(a2_sprites *s, int index, const uint8_t palette[768])
{
    uint32_t size = 0;
    const uint8_t *d = a2_archive_data(&s->archive, (size_t)index, &size);
    if (!d || size < 8)
        return 0;
    int w = d[0] | (d[1] << 8);
    int h = d[2] | (d[3] << 8);
    if (w <= 0 || h <= 0 || (size_t)8 + (size_t)w * h > size)
        return 0;

    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba)
        return 0;
    const uint8_t *px = d + 8;
    for (int i = 0; i < w * h; i++) {
        uint8_t idx = px[i];
        uint8_t *o = rgba + (size_t)i * 4;
        if (idx == SPRITE_TRANSPARENT) {
            o[0] = o[1] = o[2] = o[3] = 0;
        } else {
            o[0] = palette[idx * 3 + 0];
            o[1] = palette[idx * 3 + 1];
            o[2] = palette[idx * 3 + 2];
            o[3] = 255;
        }
    }
    a2_image im = {w, h, rgba};
    s->entries[index].tex = a2_gfx_texture_from_image_nearest(&im, 1);
    s->entries[index].w = w;
    s->entries[index].h = h;
    s->entries[index].ax = d[4] | (d[5] << 8);
    s->entries[index].ay = d[6] | (d[7] << 8);
    free(rgba);
    return 1;
}

a2_sprites *a2_sprites_load(const char *datadir, const uint8_t palette[768])
{
    char img[1024], ind[1024];
    snprintf(img, sizeof img, "%s/RC.IMG", datadir);
    snprintf(ind, sizeof ind, "%s/RC.IND", datadir);

    a2_sprites *s = calloc(1, sizeof(*s));
    if (s)
        memcpy(s->palette, palette, sizeof s->palette);
    if (!s)
        return NULL;
    if (a2_archive_open(&s->archive, img, ind) != 0) {
        free(s);
        return NULL;
    }
    s->count = (int)s->archive.count;
    s->entries = calloc(s->count ? s->count : 1, sizeof(sprite_entry));
    if (!s->entries) {
        a2_archive_close(&s->archive);
        free(s);
        return NULL;
    }
    /* The HUD sprites do not come out of the palette as the file states them.
     * Measured against the game running under Wine at 640x480x16 (etappe0,
     * demo mode), where every track uses e02_pl16.pcx in 16-bit mode:
     *
     *     index   e02_pl16         on screen
     *     15      (255,255,255)    (255,255,255)
     *      0      (0,0,0)          (8,0,0)
     *     73      (255,137,20)     (255,73,0)     the lit turbo chevrons
     *     81      (85,0,0)         (25,0,0)       the dark damage bar
     *
     * which is a gamma of about 2.0 on each channel: it fixes black and white,
     * predicts 73 as (255,74,2) and -- on a sample it was not fitted to -- 81
     * as (28,0,0). The **fonts do not follow it**: olfnt02's blue reads
     * (140,158,214) on screen against (143,165,206) in the file, so the two
     * take different paths and only the sprites are adjusted here.
     *
     * Untransformed, index 147 -- the brackets' one-pixel anti-alias edge --
     * is a (28,105,105) teal that appears in neither the running game nor the
     * reference screenshot; through this it becomes (3,43,43) and reads as the
     * dark edge both of them show.  It is the game's own doing (open question
     * 44, settled session 14): Game.ini gamma=1 makes winlib's conversion to
     * 16 bits square each channel (0x468230, c*c >> 8), and Sprite.cpp's
     * loader converts through it.  The 3D textures get the same in render.c
     * (a2_gfx_gamma_rgba). */
    uint8_t adj[768];
    for (int i = 0; i < 768; i++) {
        float v = palette[i] / 255.0f;
        adj[i] = (uint8_t)(v * v * 255.0f + 0.5f);
    }
    int ok = 0;
    for (int i = 0; i < s->count; i++)
        ok += upload(s, i, adj);
    fprintf(stderr, "sprites: %d/%d from RC.IMG\n", ok, s->count);
    return s;
}

void a2_sprites_free(a2_sprites *s)
{
    if (!s)
        return;
    for (int i = 0; i < s->count; i++)
        a2_gfx_delete_texture(s->entries[i].tex);
    free(s->entries);
    a2_archive_close(&s->archive);
    free(s);
}

int a2_sprites_count(const a2_sprites *s) { return s ? s->count : 0; }

uint8_t *a2_sprite_rgba(const a2_sprites *s, int index, int *w, int *h)
{
    if (!s || index < 0 || index >= s->count)
        return NULL;
    uint32_t size = 0;
    const uint8_t *d = a2_archive_data(&s->archive, (size_t)index, &size);
    if (!d || size < 8)
        return NULL;
    int sw = d[0] | (d[1] << 8), sh = d[2] | (d[3] << 8);
    if (sw <= 0 || sh <= 0 || (size_t)8 + (size_t)sw * sh > size)
        return NULL;
    uint8_t *rgba = malloc((size_t)sw * sh * 4);
    if (!rgba)
        return NULL;
    for (int i = 0; i < sw * sh; i++) {
        uint8_t idx = d[8 + i];
        uint8_t *o = rgba + (size_t)i * 4;
        if (idx == SPRITE_TRANSPARENT) {
            o[0] = o[1] = o[2] = o[3] = 0;
        } else {
            memcpy(o, s->palette + idx * 3, 3);
            o[3] = 255;
        }
    }
    *w = sw;
    *h = sh;
    return rgba;
}

int a2_sprite_size(const a2_sprites *s, int index, int *w, int *h)
{
    if (!s || index < 0 || index >= s->count || !s->entries[index].tex)
        return 0;
    if (w)
        *w = s->entries[index].w;
    if (h)
        *h = s->entries[index].h;
    return 1;
}

int a2_sprite_anchor(const a2_sprites *s, int index, int *ax, int *ay)
{
    if (!s || index < 0 || index >= s->count || !s->entries[index].tex)
        return 0;
    if (ax)
        *ax = s->entries[index].ax;
    if (ay)
        *ay = s->entries[index].ay;
    return 1;
}

void a2_sprites_begin(int width, int height)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    /* y down, like the original.  The depth range is generous because the 3D
     * font's glyphs are extruded meshes drawn in this same pass. */
    glOrtho(0.0, width, height, 0.0, -10000.0, 10000.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void a2_sprites_end(void)
{
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void a2_sprite_draw(const a2_sprites *s, int index, float x, float y, float scale)
{
    int w, h;
    if (!a2_sprite_size(s, index, &w, &h))
        return;
    float x1 = x + w * scale, y1 = y + h * scale;
    glBindTexture(GL_TEXTURE_2D, s->entries[index].tex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x1, y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x, y1);
    glEnd();
}

void a2_sprite_draw_sub(const a2_sprites *s, int index, int sx, int sy, int sw, int sh, float x,
                        float y, float dw, float dh)
{
    int w, h;
    if (!a2_sprite_size(s, index, &w, &h) || sw <= 0 || sh <= 0)
        return;
    float u0 = (float)sx / (float)w, u1 = (float)(sx + sw) / (float)w;
    float v0 = (float)sy / (float)h, v1 = (float)(sy + sh) / (float)h;
    glBindTexture(GL_TEXTURE_2D, s->entries[index].tex);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0);
    glVertex2f(x, y);
    glTexCoord2f(u1, v0);
    glVertex2f(x + dw, y);
    glTexCoord2f(u1, v1);
    glVertex2f(x + dw, y + dh);
    glTexCoord2f(u0, v1);
    glVertex2f(x, y + dh);
    glEnd();
}
