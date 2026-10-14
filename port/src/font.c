#include "font.h"

#include "gfx.h"
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#define GLYPH_COUNT 256
#define HEADER_BYTES (GLYPH_COUNT * 8)

typedef struct {
    int w, h;    /* glyph size in pixels; 0 when absent */
    float u0, u1; /* position in the atlas */
} glyph;

struct a2_font {
    unsigned tex;
    int atlas_w, atlas_h;
    glyph g[GLYPH_COUNT];
    int height; /* tallest glyph */
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

a2_font *a2_font_load(const a2_archive *rcs, const char *name, const uint8_t palette[768])
{
    long idx = a2_archive_find(rcs, name);
    if (idx < 0)
        return NULL;
    uint32_t size = 0;
    const uint8_t *d = a2_archive_data(rcs, (size_t)idx, &size);
    if (!d || size < HEADER_BYTES)
        return NULL;

    a2_font *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    /* Pass one: read the table and measure the atlas. */
    int total_w = 0, max_h = 0, used = 0;
    for (int c = 0; c < GLYPH_COUNT; c++) {
        uint16_t w = rd16(d + c * 8);
        uint16_t h = rd16(d + c * 8 + 2);
        uint32_t off = rd32(d + c * 8 + 4);
        /* Absent glyphs are all-0xFF; guard the rest against a bad file. */
        if (w == 0xFFFF || w == 0 || h == 0 || w > 1024 || h > 1024)
            continue;
        if ((uint64_t)off + (uint64_t)w * h > size)
            continue;
        f->g[c].w = w;
        f->g[c].h = h;
        total_w += w;
        if (h > max_h)
            max_h = h;
        used++;
    }
    if (!used || total_w <= 0) {
        free(f);
        return NULL;
    }

    /* Pass two: blit every glyph into one row-shaped atlas. */
    a2_image im;
    im.width = total_w;
    im.height = max_h;
    im.rgba = calloc((size_t)total_w * max_h, 4);
    if (!im.rgba) {
        free(f);
        return NULL;
    }
    int x0 = 0;
    for (int c = 0; c < GLYPH_COUNT; c++) {
        if (!f->g[c].w)
            continue;
        uint32_t off = rd32(d + c * 8 + 4);
        for (int y = 0; y < f->g[c].h; y++) {
            for (int x = 0; x < f->g[c].w; x++) {
                uint8_t i = d[off + (size_t)y * f->g[c].w + x];
                if (i == 0xFF)
                    continue; /* transparent, as in the sprites */
                uint8_t *p = im.rgba + (((size_t)y * total_w) + x0 + x) * 4;
                p[0] = palette[i * 3 + 0];
                p[1] = palette[i * 3 + 1];
                p[2] = palette[i * 3 + 2];
                p[3] = 255;
            }
        }
        f->g[c].u0 = (float)x0 / (float)total_w;
        f->g[c].u1 = (float)(x0 + f->g[c].w) / (float)total_w;
        x0 += f->g[c].w;
    }

    f->tex = a2_gfx_texture_from_image_nearest(&im, 1);
    free(im.rgba);
    f->atlas_w = total_w;
    f->atlas_h = max_h;
    f->height = max_h;
    if (!f->tex) {
        free(f);
        return NULL;
    }
    fprintf(stderr, "font: %s, %d glyphs, atlas %dx%d\n", name, used, total_w, max_h);
    return f;
}

void a2_font_free(a2_font *f)
{
    if (!f)
        return;
    a2_gfx_delete_texture(f->tex);
    free(f);
}

float a2_font_height(const a2_font *f, float scale)
{
    return f ? f->height * scale : 0.0f;
}

float a2_font_text_width(const a2_font *f, const char *text, float scale)
{
    if (!f || !text)
        return 0.0f;
    float w = 0.0f;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (f->g[*p].w)
            w += f->g[*p].w;
        else if (*p == ' ')
            w += f->height * 0.4f; /* no space glyph in these fonts */
    }
    return w * scale;
}

void a2_font_draw(const a2_font *f, const char *text, float x, float y, float scale)
{
    a2_font_draw_tinted(f, text, x, y, scale, 1.0f, 1.0f, 1.0f);
}

void a2_font_draw_tinted(const a2_font *f, const char *text, float x, float y, float scale,
                         float r, float g, float b)
{
    if (!f || !text)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f->tex);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_QUADS);
    float cx = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        const glyph *g = &f->g[*p];
        if (!g->w) {
            if (*p == ' ')
                cx += f->height * 0.4f * scale;
            continue;
        }
        float w = g->w * scale, h = g->h * scale;
        /* Glyphs sit on a common baseline: the atlas is as tall as the tallest
         * one, and each is stored from its own top, so draw from `y`. */
        float v1 = (float)g->h / (float)f->atlas_h;
        glTexCoord2f(g->u0, 0.0f);
        glVertex2f(cx, y);
        glTexCoord2f(g->u1, 0.0f);
        glVertex2f(cx + w, y);
        glTexCoord2f(g->u1, v1);
        glVertex2f(cx + w, y + h);
        glTexCoord2f(g->u0, v1);
        glVertex2f(cx, y + h);
        cx += w;
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
