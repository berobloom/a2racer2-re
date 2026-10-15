#include "font3d.h"

#include <GL/gl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_COUNT 49
#define FONT_TEXTURE "s_letter.pcx"

/* Mesh names in FONT3D_ANIM frame order. */
static const char *const GLYPH_MESH[GLYPH_COUNT] = {
    "LTR_0.d3d",    "LTR_1.d3d",    "LTR_2.d3d",    "LTR_3.d3d",    "LTR_4.d3d",
    "LTR_5.d3d",    "LTR_6.d3d",    "LTR_7.d3d",    "LTR_8.d3d",    "LTR_9.d3d",
    "LTR_A.d3d",    "LTR_B.d3d",    "LTR_C.d3d",    "LTR_D.d3d",    "LTR_E.d3d",
    "LTR_F.d3d",    "LTR_G.d3d",    "LTR_H.d3d",    "LTR_I.d3d",    "LTR_J.d3d",
    "LTR_K.d3d",    "LTR_L.d3d",    "LTR_M.d3d",    "LTR_N.d3d",    "LTR_O.d3d",
    "LTR_P.d3d",    "LTR_Q.d3d",    "LTR_R.d3d",    "LTR_S.d3d",    "LTR_T.d3d",
    "LTR_U.d3d",    "LTR_V.d3d",    "LTR_W.d3d",    "LTR_X.d3d",    "LTR_Y.d3d",
    "LTR_Z.d3d",    "LTR_T.d3d",    "LTR_AE.d3d",   "LTR_OE.d3d",   "LTR_SS.d3d",
    "LTR_UE.d3d",   "LTR_IS.d3d",   "LTR_AANH.d3d", "LTR_LINE.d3d", "LTR_MAAL.d3d",
    "LTR_PLUS.d3d", "LTR_PUNT.d3d", "LTR_UITR.d3d", "LTR_VRAG.d3d",
};

struct a2_font3d {
    int handle[GLYPH_COUNT];
    float width[GLYPH_COUNT];  /* in glyph-height units */
    float origin[GLYPH_COUNT]; /* left edge, so glyphs sit flush */
    float height;              /* tallest glyph, used to normalise */
    int loaded;
};

/* Map a character to a frame index, or -1 to skip. */
static int glyph_index(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (isalpha(c))
        return 10 + (toupper(c) - 'A');
    switch (c) {
    case '=':
        return 41;
    case '"':
        return 42;
    case '-':
    case '_':
        return 43;
    case '*':
    case 'x':
        return 44;
    case '+':
        return 45;
    case '.':
    case ',':
        return 46;
    case '!':
        return 47;
    case '?':
        return 48;
    default:
        return -1; /* space and anything unmapped */
    }
}

a2_font3d *a2_font3d_load(a2_scene *scene, const a2_archive *rcs)
{
    a2_font3d *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    for (int i = 0; i < GLYPH_COUNT; i++)
        f->handle[i] = -1;

    for (int i = 0; i < GLYPH_COUNT; i++) {
        int h = a2_scene_load_mesh_named(scene, rcs, GLYPH_MESH[i], FONT_TEXTURE);
        f->handle[i] = h;
        if (h < 0)
            continue;
        float lo[3], hi[3];
        if (!a2_scene_model_bounds(scene, h, lo, hi))
            continue;
        f->width[i] = hi[0] - lo[0];
        f->origin[i] = lo[0];
        float gh = hi[1] - lo[1];
        if (gh > f->height)
            f->height = gh;
        f->loaded++;
    }
    if (!f->loaded || f->height <= 0.0f) {
        fprintf(stderr, "font3d: no glyphs loaded\n");
        free(f);
        return NULL;
    }
    fprintf(stderr, "font3d: %d/%d glyphs, height %.1f units\n", f->loaded, GLYPH_COUNT, f->height);
    return f;
}

void a2_font3d_free(a2_font3d *f) { free(f); }

/* Gap between glyphs, as a fraction of glyph height. */
#define TRACKING 0.12f
#define SPACE_WIDTH 0.45f

float a2_font3d_width(const a2_font3d *f, const char *text)
{
    if (!f)
        return 0.0f;
    float w = 0.0f;
    for (const char *p = text; *p; p++) {
        int g = glyph_index((unsigned char)*p);
        w += (g < 0 || f->handle[g] < 0) ? SPACE_WIDTH : f->width[g] / f->height;
        w += TRACKING;
    }
    return w;
}

void a2_font3d_draw(const a2_font3d *f, const a2_scene *scene, const char *text, float x, float y,
                    float size)
{
    if (!f || !scene)
        return;
    /* The overlay is y-down and the glyph meshes are y-up, so flip Y and place
     * the baseline at y + size. */
    float pen = x;
    for (const char *p = text; *p; p++) {
        int g = glyph_index((unsigned char)*p);
        if (g < 0 || f->handle[g] < 0) {
            pen += (SPACE_WIDTH + TRACKING) * size;
            continue;
        }
        float scale = size / f->height;
        glPushMatrix();
        glTranslatef(pen - f->origin[g] * scale, y + size, 0.0f);
        glScalef(1.0f, -1.0f, 1.0f);
        a2_scene_draw_model_scaled(scene, f->handle[g], 0.0f, 0.0f, 0.0f, scale);
        glPopMatrix();
        pen += (f->width[g] / f->height + TRACKING) * size;
    }
}

int a2_font3d_glyph(const a2_font3d *f, char c)
{
    if (!f)
        return -1;
    int g = glyph_index((unsigned char)c);
    return g < 0 ? -1 : f->handle[g];
}

