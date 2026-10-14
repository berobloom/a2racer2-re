#include "gfx.h"

#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>

static unsigned texture_from_image(const a2_image *im, int clamp, GLint filter)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    GLint wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im->width, im->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 im->rgba);
    return tex;
}

unsigned a2_gfx_texture_from_image(const a2_image *im, int clamp)
{
    return texture_from_image(im, clamp, GL_LINEAR);
}

unsigned a2_gfx_texture_from_image_nearest(const a2_image *im, int clamp)
{
    return texture_from_image(im, clamp, GL_NEAREST);
}

unsigned a2_gfx_placeholder_texture(void)
{
    enum { N = 16 };
    uint8_t px[N * N * 4];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            /* Neutral grey checker: deliberately NOT magenta, so a missing
             * texture is never confused with the game's (255,0,127) key. */
            int on = ((x / 4) ^ (y / 4)) & 1;
            uint8_t *p = px + (y * N + x) * 4;
            p[0] = p[1] = p[2] = on ? 190 : 140;
            p[3] = 255;
        }
    a2_image im = {N, N, px};
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.width, im.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 im.rgba);
    return tex;
}

static int gamma_dark;

void a2_gfx_set_gamma(int dark)
{
    gamma_dark = dark != 0;
}

void a2_gfx_gamma_rgba(uint8_t *rgba, size_t n)
{
    if (!gamma_dark)
        return;
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) {
            unsigned v = rgba[i * 4 + c];
            rgba[i * 4 + c] = (uint8_t)((v * v) >> 8); /* 0x4682a4: imul; sar 8 */
        }
}

void a2_gfx_delete_texture(unsigned tex)
{
    GLuint t = tex;
    if (t)
        glDeleteTextures(1, &t);
}
