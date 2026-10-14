#include "skid.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRIPS 20  /* 0x4ae3d4 .. 0x4cd914, 0x1910 bytes each */
#define VERTS 100  /* per wheel (0x41df52: count + 2 < 100) */

typedef struct {
    float x, y, z;
    int alpha;
} svert;

typedef struct {
    int active;
    int count; /* vertices per wheel */
    int life, age;
    svert w[2][VERTS];
} strip;

struct a2_skid {
    strip s[STRIPS];
    int prev;    /* 0x4cc014: last frame's intensity */
    int current; /* 0x4892dc: the strip being extended */
    int fade;    /* Game.ini fade_skmarks */
    unsigned tex; /* S_SFX.PCX: the marks sample a strip of tyre tread */
};

a2_skid *a2_skid_create(int fade, unsigned tex)
{
    a2_skid *k = calloc(1, sizeof *k);
    if (k) {
        k->current = -1;
        k->fade = fade;
        k->tex = tex;
    }
    return k;
}

void a2_skid_free(a2_skid *k) { free(k); }

/* 0x41d880 for the four points: wheel at lateral a, across the motion at b. */
static void point(svert *out, float x, float y, float z, float psi, float vl, float vy, float vf,
                  float rear, float a, float b, int alpha)
{
    float n = sqrtf(vl * vl + vy * vy + vf * vf);
    float ln = 0.0f, fn = 1.0f; /* standing still: lay it sideways */
    if (n > 1e-4f) {
        ln = vl / n;
        fn = vf / n;
    }
    float lat = a + -fn * b;
    float fwd = -rear + ln * b;
    float cs = cosf(psi), sn = sinf(psi);
    out->x = x + cs * lat - sn * fwd;
    out->y = y + 1.0f;
    out->z = z + sn * lat + cs * fwd;
    out->alpha = alpha;
}

/* 0x41db50: append a cross-section for both wheels. */
static void add(strip *st, const svert p[4])
{
    st->w[0][st->count] = p[0];
    st->w[0][st->count + 1] = p[1];
    st->w[1][st->count] = p[2];
    st->w[1][st->count + 1] = p[3];
    st->count += 2;
}

void a2_skid_lay(a2_skid *k, int i, float x, float y, float z, float psi, float vl, float vy,
                 float vf, float rear, float dt)
{
    if (!k)
        return;
    if (i < 4) {
        k->prev = i;
        return;
    }
    /* 0x41da70: the alpha falls as the intensity rises */
    int alpha = 200 - 16 * (i - 4 < 0 ? 0 : i - 4);
    alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
    svert p[4];
    point(&p[0], x, y, z, psi, vl, vy, vf, rear, -7.0f, 2.0f, alpha);
    point(&p[1], x, y, z, psi, vl, vy, vf, rear, -7.0f, -2.0f, alpha);
    point(&p[2], x, y, z, psi, vl, vy, vf, rear, 7.0f, 2.0f, alpha);
    point(&p[3], x, y, z, psi, vl, vy, vf, rear, 7.0f, -2.0f, alpha);
    if (k->prev < 4) {
        /* a new strip, if one is free; otherwise try again next frame */
        int n = 0;
        while (n < STRIPS && k->s[n].active)
            n++;
        if (n == STRIPS) {
            k->prev = 3;
            return;
        }
        strip *st = &k->s[n];
        memset(st, 0, sizeof *st);
        st->active = 1;
        st->life = (int)(20.0f / dt);
        add(st, p);
        if (getenv("A2_SKID_DEBUG"))
            fprintf(stderr, "skid: strip %d at (%.0f, %.1f, %.0f) a=%d\n", n, p[0].x, p[0].y, p[0].z, alpha);
        k->current = n;
        k->prev = i;
        return;
    }
    if (k->current < 0 || !k->s[k->current].active) {
        k->prev = 3;
        k->current = -1;
        return;
    }
    strip *st = &k->s[k->current];
    if (st->count + 2 < VERTS)
        add(st, p);
    k->prev = i;
}

/* The ageing in 0x41e3a0, and 0x41e1e0's fade toward alpha 255. */
void a2_skid_update(a2_skid *k)
{
    if (!k)
        return;
    for (int n = 0; n < STRIPS; n++) {
        strip *st = &k->s[n];
        if (!st->active)
            continue;
        if (++st->age >= st->life) {
            st->active = 0;
            if (k->current == n)
                k->current = -1;
            continue;
        }
        if (!k->fade)
            continue;
        int left = st->life - st->age;
        for (int w = 0; w < 2; w++)
            for (int v = 0; v < st->count; v++) {
                int a = st->w[w][v].alpha;
                int step = (255 - a) / left;
                a += step < 1 ? 1 : step;
                st->w[w][v].alpha = a > 255 ? 255 : a;
            }
    }
}

void a2_skid_draw(const a2_skid *k)
{
    if (!k)
        return;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT |
                 GL_POLYGON_BIT);
    /* The tread strip of S_SFX (0x41dad0: u 0.453 -> 0.539 across the mark,
     * v 0.15625), modulated by the vertex colour; colour-keyed texels are
     * not drawn. */
    if (k->tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, k->tex);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.0f);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
    }
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    /* SRCBLEND = INVSRCALPHA (6), DESTBLEND = SRCALPHA (5), at 0x41e44d */
    glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    for (int n = 0; n < STRIPS; n++) {
        const strip *st = &k->s[n];
        if (!st->active || st->count < 4)
            continue;
        for (int w = 0; w < 2; w++) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int v = 0; v < st->count; v++) {
                const svert *p = &st->w[w][v];
                glTexCoord2f((v & 1) ? 0.5390625f : 0.453125f, 0.15625f);
                if (getenv("A2_SKID_DEBUG"))
                    glColor4ub(255, 0, 0, 0); /* opaque red under this blend */
                else
                    glColor4ub(0x40, 0x40, 0x40, (unsigned char)p->alpha); /* 0x404040 */
                glVertex3f(p->x, p->y, p->z);
            }
            glEnd();
        }
    }
    glPopAttrib();
}
