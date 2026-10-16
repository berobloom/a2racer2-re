/* Probe what Emscripten's LEGACY_GL_EMULATION does with the port's GL usage.
 * Five panels left to right:
 *   1 immediate-mode coloured quad
 *   2 textured quad, 64x64 texture, GL_REPEAT
 *   3 textured quad, 60x40 (not a power of two), GL_REPEAT
 *   4 glDrawElements, client arrays, GL_UNSIGNED_INT indices
 *   5 glDrawElements, client arrays, GL_UNSIGNED_SHORT indices
 * Build (from port/web; -DMENU_FIRST draws with an SDL renderer first, as the
 * menu does before a race):
 *   emcc -O1 [-DMENU_FIRST] -include glshim.h gltest.c glshim.c -sUSE_SDL=2 \
 *        -sLEGACY_GL_EMULATION=1 -sASYNCIFY -o gltest.html
 * Without the shim (no -include, no glshim.c, no a2web_gl_reset call) panel 3
 * is black and panel 4 empty, and with MENU_FIRST nothing draws at all. */
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <stdlib.h>

static GLuint tex(int w, int h)
{
    unsigned char *p = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned char *o = p + 4 * (y * w + x);
            int c = ((x / 8) ^ (y / 8)) & 1;
            o[0] = c ? 255 : 30;
            o[1] = c ? 200 : 30;
            o[2] = 40;
            o[3] = 255;
        }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    free(p);
    return t;
}

static void quad(float x0, float x1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x0, -0.5f);
    glTexCoord2f(1, 0); glVertex2f(x1, -0.5f);
    glTexCoord2f(1, 1); glVertex2f(x1, 0.5f);
    glTexCoord2f(0, 1); glVertex2f(x0, 0.5f);
    glEnd();
}

int main(void)
{
    SDL_Init(SDL_INIT_VIDEO);
#ifdef MENU_FIRST
    /* as the menu does before a race: an SDL_Renderer on the canvas, then gone */
    {
        SDL_Window *mw = SDL_CreateWindow("menu", 0, 0, 640, 480, SDL_WINDOW_RESIZABLE);
        SDL_Renderer *r = SDL_CreateRenderer(mw, -1, 0);
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 64, 64);
        static unsigned px[64 * 64];
        for (int f = 0; f < 3; f++) {
            SDL_UpdateTexture(t, NULL, px, 256);
            SDL_RenderClear(r);
            SDL_RenderCopy(r, t, NULL, NULL);
            SDL_RenderPresent(r);
            emscripten_sleep(16);
        }
        SDL_DestroyTexture(t);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(mw);
    }
#endif
    SDL_Window *w = SDL_CreateWindow("gltest", 0, 0, 1000, 200, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(w);
    a2web_gl_reset();
    GLuint t64 = tex(64, 64), t60 = tex(60, 40);
    static float v[] = {0.25f, -0.5f, 0, 0.55f, -0.5f, 0, 0.55f, 0.5f, 0, 0.25f, 0.5f, 0};
    static unsigned char col[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    static unsigned int i32[] = {0, 1, 2, 0, 2, 3};
    static unsigned short i16[] = {0, 1, 2, 0, 2, 3};
    for (;;) {
        glClearColor(0.2f, 0.2f, 0.3f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_TEXTURE_2D);
        glColor4f(1, 0.3f, 0.3f, 1);
        quad(-0.95f, -0.65f);
        glColor4f(1, 1, 1, 1);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t64);
        quad(-0.55f, -0.25f);
        glBindTexture(GL_TEXTURE_2D, t60);
        quad(-0.15f, 0.15f);
        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, v);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, i32);
        glTranslatef(0.4f, 0, 0);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, i16);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        SDL_GL_SwapWindow(w);
        emscripten_sleep(16);
    }
}
