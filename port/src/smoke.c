#include "smoke.h"

#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>

#define PUFFS 70 /* 0x4d3a18 .. 0x4d8d48, 300 bytes each */

typedef struct {
    int active;
    float x, y, z;
    float vx, vy, vz;
    int frames; /* lifetime: 4 / dt (0x41fe58) */
    int age;
    float size; /* 7, growing by 29 / frames a frame (0x41f894) */
    int alpha;  /* 0x50, falling by max(1, 0x50 / frames) a frame */
} puff;

struct a2_smoke {
    puff p[PUFFS];
    unsigned rng;
};

static int rnd(a2_smoke *s)
{
    s->rng = s->rng * 214013u + 2531011u;
    return (int)((s->rng >> 16) & 0x7fff);
}

a2_smoke *a2_smoke_create(void)
{
    a2_smoke *s = calloc(1, sizeof *s);
    if (s)
        s->rng = 12345u;
    return s;
}

void a2_smoke_free(a2_smoke *s) { free(s); }

/* 0x41fd80 for one puff. */
static void one(a2_smoke *s, float x, float y, float z, float vx, float vz, float dt)
{
    int i = 0;
    while (i < PUFFS && s->p[i].active)
        i++;
    if (i == PUFFS)
        return;
    puff *p = &s->p[i];
    memset(p, 0, sizeof *p);
    p->active = 1;
    p->x = x;
    p->y = y;
    p->z = z;
    p->frames = (int)(4.0f / dt);
    if (p->frames < 1)
        p->frames = 1;
    /* each axis scaled by a random 0.75..1.0 (rand() * 7.63e-6 + 0.75) */
    p->vx = vx * ((float)rnd(s) * 7.62962736e-06f + 0.75f);
    p->vz = vz * ((float)rnd(s) * 7.62962736e-06f + 0.75f);
    /* it rises (rand() % 35 + 9) units over its life; the y the spawner
     * passes is ignored */
    p->vy = (float)(rnd(s) % 35 + 9) / (float)p->frames;
    p->size = 7.0f;
    p->alpha = 0x50;
}

void a2_smoke_spawn(a2_smoke *s, float x, float y, float z, float vx, float vz, float dt)
{
    if (!s || dt <= 0.0f || 1.0f / dt <= 12.0f) /* not below 12 fps (0x42001c) */
        return;
    one(s, x, y, z, vx, vz, dt);
    one(s, x, y, z, vx, vz, dt);
}

/* 0x41fae0. */
void a2_smoke_update(a2_smoke *s)
{
    if (!s)
        return;
    for (int i = 0; i < PUFFS; i++) {
        puff *p = &s->p[i];
        if (!p->active)
            continue;
        int fade = 0x50 / p->frames; /* an integer division: 0 for a long life */
        if (fade < 1)
            fade = 1;
        p->alpha -= fade;
        p->vx *= 0.9f;
        p->vy *= 0.9f;
        p->vz *= 0.9f;
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        p->size += 29.0f / (float)p->frames;
        if (p->alpha <= 0 || ++p->age >= p->frames)
            p->active = 0;
    }
}

/* The 6-vertex fan of 0x4200c0: centre (132, 132, 164) at the puff's alpha,
 * the four corners at +-size/2 fully transparent. */
void a2_smoke_draw(const a2_smoke *s)
{
    if (!s)
        return;
    float m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    float rx = m[0], ry = m[4], rz = m[8]; /* camera right, in world space */
    float ux = m[1], uy = m[5], uz = m[9]; /* camera up */
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < PUFFS; i++) {
        const puff *p = &s->p[i];
        if (!p->active)
            continue;
        float h = p->size * 0.5f;
        float corners[5][2] = {{-h, -h}, {-h, h}, {h, h}, {h, -h}, {-h, -h}};
        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(0x84, 0x84, 0xa4, (unsigned char)(p->alpha > 0 ? p->alpha : 0));
        glVertex3f(p->x, p->y, p->z);
        for (int k = 0; k < 5; k++) {
            float a = corners[k][0], b = corners[k][1];
            glColor4ub(0x64, 0x64, 0x64, 0);
            glVertex3f(p->x + rx * a + ux * b, p->y + ry * a + uy * b, p->z + rz * a + uz * b);
        }
        glEnd();
    }
    glPopAttrib();
}
