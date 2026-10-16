/* Web build only (force-included by web/build.sh): the few fixed-function
 * calls Emscripten's LEGACY_GL_EMULATION does not give us.
 *
 * The port's enable state is tracked here, so glIsEnabled answers for the
 * fixed-function capabilities (GL_TEXTURE_2D, GL_ALPHA_TEST) that WebGL itself
 * does not know, and glPushAttrib / glPopAttrib restore the enables, the
 * depth mask and the blend function -- everything the port's three
 * push/pop pairs change between them. */
#ifndef A2_WEB_GLSHIM_H
#define A2_WEB_GLSHIM_H

#include <GL/gl.h>

void a2web_glEnable(GLenum cap);
void a2web_glDisable(GLenum cap);
GLboolean a2web_glIsEnabled(GLenum cap);
void a2web_glDepthMask(GLboolean flag);
void a2web_glBlendFunc(GLenum s, GLenum d);
void a2web_glPushAttrib(GLbitfield mask);
void a2web_glPopAttrib(void);
/* forget the tracked state: a new context starts from GL's defaults */
void a2web_gl_reset(void);

/* WebGL 1 limits the emulation works under (seen with web/gltest.c):
 * a texture whose sides are not powers of two samples black with GL_REPEAT,
 * so such textures are scaled up to the next powers of two on upload (the
 * port's coordinates are 0..1, so they do not change); and glDrawElements
 * from client memory draws nothing with GL_UNSIGNED_INT indices, so those are
 * turned into GL_UNSIGNED_SHORT batches, the vertex pointers moved along so
 * each batch's indices fit in 16 bits. */
void a2web_glBindTexture(GLenum target, GLuint tex);
void a2web_glPixelStorei(GLenum pname, GLint param);
void a2web_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                        GLint border, GLenum fmt, GLenum type, const void *px);
void a2web_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                           GLenum fmt, GLenum type, const void *px);
void a2web_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void a2web_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void a2web_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void a2web_glEnableClientState(GLenum a);
void a2web_glDisableClientState(GLenum a);
void a2web_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx);

#ifndef A2_WEB_GLSHIM_IMPL
#define glEnable a2web_glEnable
#define glDisable a2web_glDisable
#define glIsEnabled a2web_glIsEnabled
#define glDepthMask a2web_glDepthMask
#define glBlendFunc a2web_glBlendFunc
#define glPushAttrib a2web_glPushAttrib
#define glPopAttrib a2web_glPopAttrib
#define glBindTexture a2web_glBindTexture
#define glPixelStorei a2web_glPixelStorei
#define glTexImage2D a2web_glTexImage2D
#define glTexSubImage2D a2web_glTexSubImage2D
#define glVertexPointer a2web_glVertexPointer
#define glTexCoordPointer a2web_glTexCoordPointer
#define glColorPointer a2web_glColorPointer
#define glEnableClientState a2web_glEnableClientState
#define glDisableClientState a2web_glDisableClientState
#define glDrawElements a2web_glDrawElements
#endif

#endif
