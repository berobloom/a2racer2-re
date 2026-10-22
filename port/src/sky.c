#include "sky.h"

#include "gfx.h"
#include "image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The shipped mode is 640x480 and the hires panorama 960 wide, so one screen
 * shows exactly two thirds of it.  Keeping the *fraction* rather than the
 * pixel count means a wider window shows the same slice of sky, which is what
 * matches the original's field of view. */
#define SKY_WINDOW (640.0f / 960.0f)

struct a2_sky {
    unsigned tex;
    int still; /* a back<N>.pcx viewport background */
};

a2_sky *a2_sky_load(const a2_archive *rcs, int back)
{
    char name[16] = "SCRL_HI.pcx";
    if (back >= 1 && back <= 7)
        snprintf(name, sizeof name, "back%d.pcx", back);
    long idx = a2_archive_find(rcs, name);
    if (idx < 0)
        return NULL;
    uint32_t size = 0;
    const uint8_t *data = a2_archive_data(rcs, (size_t)idx, &size);
    a2_image im;
    if (!data || a2_image_decode(&im, data, size, name) != 0)
        return NULL;
    /* No colour key: the panorama is opaque, and keying it would punch holes
     * in any cloud that happens to hit the key colour. */
    a2_sky *s = calloc(1, sizeof(*s));
    if (!s) {
        a2_image_free(&im);
        return NULL;
    }
    s->still = back >= 1 && back <= 7;
    /* LIKELY for the panorama, a PCX like the rest; back<N> is not darkened
     * (fitted against a Wine capture, session 16) */
    if (!s->still)
        a2_gfx_gamma_rgba(im.rgba, (size_t)im.width * im.height);
    s->tex = a2_gfx_texture_from_image(&im, 0); /* repeat, so the wrap is free */
    fprintf(stderr, "sky: %s (%dx%d)\n", name, im.width, im.height);
    a2_image_free(&im);
    if (!s->tex) {
        free(s);
        return NULL;
    }
    return s;
}

void a2_sky_free(a2_sky *s)
{
    if (!s)
        return;
    a2_gfx_delete_texture(s->tex);
    free(s);
}

unsigned a2_sky_horizon(int n_chunks, int start, int chunk)
{
    if (n_chunks <= 0)
        return 0xc83737;
    /* f = (2 |chunk - start| mod 2n) / n, 0..2 over a lap; every conversion
     * is _ftol, which truncates (checked against the original under Unicorn,
     * emu_skytint.py: 200 of 200) */
    int d = chunk - start;
    double f = (double)((2 * (d < 0 ? -d : d)) % (2 * n_chunks)) / n_chunks;
    double b = fabs(sin(f * M_PI)), c = fabs(sin(f * M_PI / 4.0));
    double base[3] = {200.0 + 25.0 * c, 55.0 + 175.0 * c, 55.0};
    unsigned rgb = 0;
    for (int k = 0; k < 3; k++)
        rgb = (rgb << 8) | (unsigned)((int)((255.0 - base[k]) * b) + (int)base[k]);
    return rgb;
}

void a2_sky_draw(const a2_sky *s, float yaw_deg, float heading, unsigned horizon, int w, int h)
{
    if (!s || w <= 0 || h <= 0)
        return;

    /* scroll_x = (T[heading] + 1) * -(imgW/2), wrapped -- as a fraction of the
     * image width that is (sin(yaw) + 1) * -0.5 wrapped into [0, 1). */
    float u0 = (sinf(yaw_deg * (float)M_PI / 180.0f) + 1.0f) * -0.5f;
    u0 -= floorf(u0);
    float du = SKY_WINDOW;

    /* Band height: the original's screen_h - screen_h/3. */
    float band = (float)h - (float)h / 3.0f;
    if (s->still) {
        /* 0x419145..0x4191a3: u = 1 - (heading & 0x3ff) / 1024 .. + 0.15 */
        u0 = 1.0f - (float)((int)heading & 0x3ff) / 1024.0f;
        du = 0.15f;
    }

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, w, h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    /* glOrtho with y running down flips the handedness, so the quad's winding
     * comes out back-facing; the scene pass leaves culling on. */
    GLboolean culling = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    if (s->still) {
        /* the two quads of 0x418ab0: white at the top and bottom edges, the
         * horizon tint along the split a third of the way down, modulated */
        float split = (float)h / 3.0f;
        float hr = (float)((horizon >> 16) & 0xff) / 255.0f, hg = (float)((horizon >> 8) & 0xff) / 255.0f,
              hb = (float)(horizon & 0xff) / 255.0f;
        const float ys[3] = {0.0f, split, (float)h}, vs[3] = {0.0f, 1.0f / 3.0f, 1.0f};
        for (int q = 0; q < 2; q++) {
            for (int k = 0; k < 4; k++) {
                int row = q + (k >= 2);
                int edge = row != 1;
                glColor3f(edge ? 1.0f : hr, edge ? 1.0f : hg, edge ? 1.0f : hb);
                float u = (k == 1 || k == 2) ? u0 + du : u0;
                glTexCoord2f(u, vs[row]);
                glVertex2f((k == 1 || k == 2) ? (float)w : 0.0f, ys[row]);
            }
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glTexCoord2f(u0, 0.0f);
        glVertex2f(0.0f, 0.0f);
        glTexCoord2f(u0 + du, 0.0f);
        glVertex2f((float)w, 0.0f);
        glTexCoord2f(u0 + du, 1.0f);
        glVertex2f((float)w, band);
        glTexCoord2f(u0, 1.0f);
        glVertex2f(0.0f, band);
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    if (culling)
        glEnable(GL_CULL_FACE);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}
