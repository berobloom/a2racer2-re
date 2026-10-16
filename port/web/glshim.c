/* See glshim.h. */
#define A2_WEB_GLSHIM_IMPL
#include "glshim.h"

/* The build force-includes glshim.h into every file, this one too and before
 * the define above, so the renames are in force here: undo them, or each
 * wrapper would call itself. */
#undef glEnable
#undef glDisable
#undef glIsEnabled
#undef glDepthMask
#undef glBlendFunc
#undef glPushAttrib
#undef glPopAttrib
#undef glBindTexture
#undef glPixelStorei
#undef glTexImage2D
#undef glTexSubImage2D
#undef glVertexPointer
#undef glTexCoordPointer
#undef glColorPointer
#undef glEnableClientState
#undef glDisableClientState
#undef glDrawElements

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

/* The capabilities the port enables and disables. */
static const GLenum CAPS[] = {GL_TEXTURE_2D,    GL_BLEND,        GL_DEPTH_TEST,
                              GL_ALPHA_TEST,    GL_CULL_FACE,    GL_SCISSOR_TEST,
                              GL_POLYGON_OFFSET_FILL};
enum { NCAPS = sizeof CAPS / sizeof CAPS[0], DEPTH = 8 };

typedef struct {
    unsigned char on[NCAPS];
    GLboolean depth_mask;
    GLenum blend_s, blend_d;
} state;

static state cur = {{0}, GL_TRUE, GL_ONE, GL_ZERO};
static state stack[DEPTH];
static int top;

static int find(GLenum cap)
{
    for (int i = 0; i < NCAPS; i++)
        if (CAPS[i] == cap)
            return i;
    return -1;
}

void a2web_glEnable(GLenum cap)
{
    int i = find(cap);
    if (i >= 0)
        cur.on[i] = 1;
    glEnable(cap);
}

void a2web_glDisable(GLenum cap)
{
    int i = find(cap);
    if (i >= 0)
        cur.on[i] = 0;
    glDisable(cap);
}

GLboolean a2web_glIsEnabled(GLenum cap)
{
    int i = find(cap);
    return i >= 0 ? (GLboolean)cur.on[i] : glIsEnabled(cap);
}

void a2web_glDepthMask(GLboolean flag)
{
    cur.depth_mask = flag;
    glDepthMask(flag);
}

void a2web_glBlendFunc(GLenum s, GLenum d)
{
    cur.blend_s = s;
    cur.blend_d = d;
    glBlendFunc(s, d);
}

void a2web_glPushAttrib(GLbitfield mask)
{
    (void)mask; /* everything tracked is saved whatever the mask */
    if (top < DEPTH)
        stack[top] = cur;
    top++;
}

void a2web_glPopAttrib(void)
{
    if (top <= 0)
        return;
    top--;
    if (top >= DEPTH)
        return;
    const state *s = &stack[top];
    for (int i = 0; i < NCAPS; i++)
        if (s->on[i] != cur.on[i])
            (s->on[i] ? a2web_glEnable : a2web_glDisable)(CAPS[i]);
    if (s->depth_mask != cur.depth_mask)
        a2web_glDepthMask(s->depth_mask);
    if (s->blend_s != cur.blend_s || s->blend_d != cur.blend_d)
        a2web_glBlendFunc(s->blend_s, s->blend_d);
}

/* ------------------------------------------------------------ textures */

enum { MAXTEX = 8192 };
static struct { unsigned short w, h, W, H; } texsize[MAXTEX]; /* by name */
static GLuint bound;
static GLint unpack_align = 4;

static int pot(int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

static int bpp(GLenum fmt)
{
    return fmt == GL_RGB ? 3 : fmt == GL_RGBA ? 4 : 1;
}

static size_t row_bytes(int w, int b)
{
    size_t r = (size_t)w * b, a = (size_t)unpack_align;
    return (r + a - 1) / a * a;
}

void a2web_glBindTexture(GLenum target, GLuint tex)
{
    if (target == GL_TEXTURE_2D)
        bound = tex;
    glBindTexture(target, tex);
}

void a2web_glPixelStorei(GLenum pname, GLint param)
{
    if (pname == GL_UNPACK_ALIGNMENT)
        unpack_align = param;
    glPixelStorei(pname, param);
}

void a2web_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                        GLint border, GLenum fmt, GLenum type, const void *px)
{
    int W = pot(w), H = pot(h), b = bpp(fmt);
    if (bound < MAXTEX) {
        texsize[bound].w = (unsigned short)w;
        texsize[bound].h = (unsigned short)h;
        texsize[bound].W = (unsigned short)W;
        texsize[bound].H = (unsigned short)H;
    }
    if ((W == w && H == h) || type != GL_UNSIGNED_BYTE || level != 0) {
        glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
        return;
    }
    unsigned char *o = malloc((size_t)W * H * b);
    if (!o) {
        glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
        return;
    }
    if (px) {
        size_t rb = row_bytes(w, b);
        for (int y = 0; y < H; y++) {
            const unsigned char *src = (const unsigned char *)px + rb * (size_t)(y * h / H);
            for (int x = 0; x < W; x++)
                memcpy(o + ((size_t)y * W + x) * b, src + (size_t)(x * w / W) * b, (size_t)b);
        }
    } else {
        memset(o, 0, (size_t)W * H * b);
    }
    GLint keep = unpack_align;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(target, level, ifmt, W, H, border, fmt, type, o);
    glPixelStorei(GL_UNPACK_ALIGNMENT, keep);
    free(o);
}

void a2web_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                           GLenum fmt, GLenum type, const void *px)
{
    int tw = bound < MAXTEX ? texsize[bound].w : 0, th = bound < MAXTEX ? texsize[bound].h : 0;
    int TW = bound < MAXTEX ? texsize[bound].W : 0, TH = bound < MAXTEX ? texsize[bound].H : 0;
    if (!tw || (TW == tw && TH == th) || type != GL_UNSIGNED_BYTE || level != 0) {
        glTexSubImage2D(target, level, x, y, w, h, fmt, type, px);
        return;
    }
    /* the same rectangle in the scaled copy, filled by nearest sampling */
    int x0 = x * TW / tw, x1 = (x + w) * TW / tw, y0 = y * TH / th, y1 = (y + h) * TH / th;
    if (x1 <= x0 || y1 <= y0)
        return;
    int b = bpp(fmt), ow = x1 - x0, oh = y1 - y0;
    unsigned char *o = malloc((size_t)ow * oh * b);
    if (!o)
        return;
    size_t rb = row_bytes(w, b);
    for (int j = 0; j < oh; j++) {
        int sy = (y0 + j) * th / TH - y;
        sy = sy < 0 ? 0 : sy >= h ? h - 1 : sy;
        for (int i = 0; i < ow; i++) {
            int sx = (x0 + i) * tw / TW - x;
            sx = sx < 0 ? 0 : sx >= w ? w - 1 : sx;
            memcpy(o + ((size_t)j * ow + i) * b,
                   (const unsigned char *)px + rb * (size_t)sy + (size_t)sx * b, (size_t)b);
        }
    }
    GLint keep = unpack_align;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(target, level, x0, y0, ow, oh, fmt, type, o);
    glPixelStorei(GL_UNPACK_ALIGNMENT, keep);
    free(o);
}

/* --------------------------------------------------------- indexed draws */

enum { A_VERTEX, A_TEXCOORD, A_COLOR, NARR };
static struct {
    int on;
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *p;
} arr[NARR];

static int type_size(GLenum t)
{
    return t == GL_UNSIGNED_BYTE || t == GL_BYTE ? 1 : t == GL_SHORT || t == GL_UNSIGNED_SHORT ? 2 : 4;
}

static void set_ptr(int a, const void *p)
{
    if (a == A_VERTEX)
        glVertexPointer(arr[a].size, arr[a].type, arr[a].stride, p);
    else if (a == A_TEXCOORD)
        glTexCoordPointer(arr[a].size, arr[a].type, arr[a].stride, p);
    else
        glColorPointer(arr[a].size, arr[a].type, arr[a].stride, p);
}

static void record(int a, GLint size, GLenum type, GLsizei stride, const void *p)
{
    arr[a].size = size;
    arr[a].type = type;
    arr[a].stride = stride;
    arr[a].p = p;
    set_ptr(a, p);
}

void a2web_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    record(A_VERTEX, size, type, stride, p);
}

void a2web_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    record(A_TEXCOORD, size, type, stride, p);
}

void a2web_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    record(A_COLOR, size, type, stride, p);
}

static int arr_of(GLenum a)
{
    return a == GL_VERTEX_ARRAY ? A_VERTEX : a == GL_TEXTURE_COORD_ARRAY ? A_TEXCOORD
         : a == GL_COLOR_ARRAY ? A_COLOR : -1;
}

void a2web_glEnableClientState(GLenum a)
{
    int i = arr_of(a);
    if (i >= 0)
        arr[i].on = 1;
    glEnableClientState(a);
}

void a2web_glDisableClientState(GLenum a)
{
    int i = arr_of(a);
    if (i >= 0)
        arr[i].on = 0;
    glDisableClientState(a);
}

/* draw triangles idx[first..first+n) rebased on `base` */
static void draw_batch(GLenum mode, const GLuint *idx, int n, GLuint base, GLushort *tmp)
{
    for (int k = 0; k < n; k++)
        tmp[k] = (GLushort)(idx[k] - base);
    for (int a = 0; a < NARR; a++)
        if (arr[a].on && arr[a].p) {
            size_t st = arr[a].stride ? (size_t)arr[a].stride
                                      : (size_t)arr[a].size * type_size(arr[a].type);
            set_ptr(a, (const char *)arr[a].p + st * base);
        }
    glDrawElements(mode, n, GL_UNSIGNED_SHORT, tmp);
}

void a2web_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if (type != GL_UNSIGNED_INT || mode != GL_TRIANGLES || count <= 0) {
        glDrawElements(mode, count, type, indices);
        return;
    }
    const GLuint *idx = indices;
    static GLushort *tmp;
    static int tmp_n;
    if (tmp_n < count) {
        free(tmp);
        tmp = malloc(sizeof *tmp * (size_t)count);
        tmp_n = tmp ? count : 0;
        if (!tmp)
            return;
    }
    /* whole triangles into batches whose index span fits 16 bits */
    int start = 0;
    GLuint lo = 0xffffffffu, hi = 0;
    for (int t = 0; t + 2 < count; t += 3) {
        GLuint tl = idx[t], th = idx[t];
        for (int k = 1; k < 3; k++) {
            if (idx[t + k] < tl)
                tl = idx[t + k];
            if (idx[t + k] > th)
                th = idx[t + k];
        }
        GLuint nl = tl < lo ? tl : lo, nh = th > hi ? th : hi;
        if (t > start && nh - nl > 65535u) {
            draw_batch(mode, idx + start, t - start, lo, tmp);
            start = t;
            nl = tl;
            nh = th;
        }
        lo = nl;
        hi = nh;
    }
    if (count - start >= 3)
        draw_batch(mode, idx + start, (count - start) / 3 * 3, lo, tmp);
    for (int a = 0; a < NARR; a++) /* the caller's pointers back */
        if (arr[a].on && arr[a].p)
            set_ptr(a, arr[a].p);
}

/* A new GL context starts from the defaults (each race makes its own). */
void a2web_gl_reset(void)
{
    /* A canvas has one WebGL context: the race's "new" context is the one
     * SDL's renderer drew the menu with, left with its program, buffers and
     * vertex attributes, which the fixed-function emulation does not expect
     * (every draw then failed: "no buffer is bound to enabled attribute").
     * Put the raw context back to its defaults.  The menu needs the same
     * after a race: the race's culling (clockwise front faces) dropped one
     * of the two triangles SDL's renderer draws the screen with. */
    EM_ASM({
        var gl = typeof GLctx !== 'undefined' ? GLctx : null;
        if (!gl)
            return;
        var n = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
        for (var i = 0; i < n; i++)
            gl.disableVertexAttribArray(i);
        gl.bindBuffer(gl.ARRAY_BUFFER, null);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
        gl.useProgram(null);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, null);
        gl.disable(gl.BLEND);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.frontFace(gl.CCW);
        gl.cullFace(gl.BACK);
        gl.disable(gl.SCISSOR_TEST);
        gl.disable(gl.POLYGON_OFFSET_FILL);
        gl.depthMask(true);
        gl.colorMask(true, true, true, true);
        gl.blendFunc(gl.ONE, gl.ZERO);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
        gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
    });
    memset(&cur, 0, sizeof cur);
    cur.depth_mask = GL_TRUE;
    cur.blend_s = GL_ONE;
    cur.blend_d = GL_ZERO;
    top = 0;
    memset(texsize, 0, sizeof texsize);
    bound = 0;
    unpack_align = 4;
    memset(arr, 0, sizeof arr);
}
