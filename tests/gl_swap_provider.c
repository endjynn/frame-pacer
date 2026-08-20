#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static unsigned int glx_calls, egl_calls, vertices;
static GLboolean native_vertex_shader;
static GLboolean egl_context_active;
static GLint framebuffer = 7, active_texture = 0x84C3, texture = 19, sampler = 23;
static GLint program = 29, vao = 31, array_buffer = 37;
static GLint viewport[4] = {41, 43, 1920, 1200};
static GLint scissor_box[4] = {47, 53, 1800, 1000};
static GLint color_mask[4] = {1, 0, 1, 0};
static GLint blend_source_rgb = GL_ONE, blend_destination_rgb = GL_ZERO;
static GLint blend_source_alpha = GL_ONE, blend_destination_alpha = GL_ZERO;
static GLint blend_equation_rgb = 0x8006, blend_equation_alpha = 0x8006;
static GLboolean blend, cull = GL_TRUE, depth = GL_TRUE, stencil = GL_TRUE, scissor = GL_TRUE, srgb = GL_TRUE;

void glXSwapBuffers(Display *display, GLXDrawable drawable)
{
    (void)display;
    (void)drawable;
    ++glx_calls;
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    (void)display;
    (void)surface;
    ++egl_calls;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    (void)display;
    (void)surface;
    (void)rects;
    (void)count;
    ++egl_calls;
    return EGL_TRUE;
}

EGLContext eglGetCurrentContext(void)
{ return egl_context_active ? (EGLContext)(uintptr_t)2 : EGL_NO_CONTEXT; }
EGLBoolean eglQuerySurface(EGLDisplay display, EGLSurface surface, EGLint attribute,
                           EGLint *value)
{
    (void)display;
    (void)surface;
    if (!value) return EGL_FALSE;
    *value = attribute == EGL_WIDTH ? 1920 : attribute == EGL_HEIGHT ? 1200 : 0;
    return EGL_TRUE;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    __GLXextFuncPtr result = 0;
    EGLBoolean (*egl_swap)(EGLDisplay, EGLSurface);

    if (!name || strcmp((const char *)name, "eglSwapBuffers")) return 0;
    egl_swap = eglSwapBuffers;
    _Static_assert(sizeof(result) == sizeof(egl_swap), "unsupported function pointer representation");
    memcpy(&result, &egl_swap, sizeof(result));
    return result;
}

unsigned int frame_pacer_test_glx_calls(void) { return glx_calls; }
unsigned int frame_pacer_test_egl_calls(void) { return egl_calls; }
void frame_pacer_test_set_egl_context(GLboolean active) { egl_context_active = active; }
unsigned int frame_pacer_test_gl_vertices(void) { return vertices; }
unsigned int frame_pacer_test_native_vertex_shader(void) { return native_vertex_shader; }
unsigned int frame_pacer_test_gl_state_preserved(void)
{
    unsigned int result = 0;
    if (framebuffer == 7) result |= 1U << 0;
    if (active_texture == 0x84C3) result |= 1U << 1;
    if (texture == 19) result |= 1U << 2;
    if (sampler == 23) result |= 1U << 3;
    if (program == 29) result |= 1U << 4;
    if (vao == 31) result |= 1U << 5;
    if (array_buffer == 37) result |= 1U << 6;
    if (viewport[0] == 41 && viewport[1] == 43 && viewport[2] == 1920 && viewport[3] == 1200) result |= 1U << 7;
    if (scissor_box[0] == 47 && scissor_box[1] == 53 && scissor_box[2] == 1800 && scissor_box[3] == 1000) result |= 1U << 8;
    if (color_mask[0] == 1 && color_mask[1] == 0 && color_mask[2] == 1 && color_mask[3] == 0) result |= 1U << 9;
    if (!blend && cull && depth && stencil && scissor && srgb) result |= 1U << 10;
    return result;
}

GLXContext glXGetCurrentContext(void)
{ return egl_context_active ? 0 : (GLXContext)(uintptr_t)1; }
const GLubyte *glGetString(GLenum parameter)
{ (void)parameter; return (const GLubyte *)"4.6 frame-pacer test"; }
void glGetIntegerv(GLenum parameter, GLint *value)
{
    value[0] = 0;
    if (parameter == 0x8CA6) value[0] = framebuffer;
    else if (parameter == 0x84E0) value[0] = active_texture;
    else if (parameter == 0x8069) value[0] = texture;
    else if (parameter == 0x8919) value[0] = sampler;
    else if (parameter == 0x8B8D) value[0] = program;
    else if (parameter == 0x85B5) value[0] = vao;
    else if (parameter == 0x8894) value[0] = array_buffer;
    else if (parameter == GL_VIEWPORT) memcpy(value, viewport, sizeof(viewport));
    else if (parameter == 0x0C10) memcpy(value, scissor_box, sizeof(scissor_box));
    else if (parameter == 0x0C23) memcpy(value, color_mask, sizeof(color_mask));
    else if (parameter == 0x80C9) value[0] = blend_source_rgb;
    else if (parameter == 0x80C8) value[0] = blend_destination_rgb;
    else if (parameter == 0x80CB) value[0] = blend_source_alpha;
    else if (parameter == 0x80CA) value[0] = blend_destination_alpha;
    else if (parameter == 0x8009) value[0] = blend_equation_rgb;
    else if (parameter == 0x883D) value[0] = blend_equation_alpha;
}
void glXQueryDrawable(Display *display, GLXDrawable drawable, int attribute, unsigned int *value)
{
    (void)display; (void)drawable;
    *value = attribute == GLX_WIDTH ? 1920U : attribute == GLX_HEIGHT ? 1200U : 0U;
}
void glPushAttrib(GLbitfield value) { (void)value; }
void glPopAttrib(void) {}
void glMatrixMode(GLenum value) { (void)value; }
void glPushMatrix(void) {}
void glPopMatrix(void) {}
void glLoadIdentity(void) {}
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near, GLdouble far)
{ (void)left; (void)right; (void)bottom; (void)top; (void)near; (void)far; }
void glDisable(GLenum value)
{ if (value == GL_BLEND) blend = GL_FALSE; else if (value == GL_CULL_FACE) cull = GL_FALSE; else if (value == GL_DEPTH_TEST) depth = GL_FALSE; else if (value == GL_STENCIL_TEST) stencil = GL_FALSE; else if (value == GL_SCISSOR_TEST) scissor = GL_FALSE; else if (value == 0x8DB9) srgb = GL_FALSE; }
void glEnable(GLenum value)
{ if (value == GL_BLEND) blend = GL_TRUE; else if (value == GL_CULL_FACE) cull = GL_TRUE; else if (value == GL_DEPTH_TEST) depth = GL_TRUE; else if (value == GL_STENCIL_TEST) stencil = GL_TRUE; else if (value == GL_SCISSOR_TEST) scissor = GL_TRUE; else if (value == 0x8DB9) srgb = GL_TRUE; }
GLboolean glIsEnabled(GLenum value)
{ if (value == GL_BLEND) return blend; if (value == GL_CULL_FACE) return cull; if (value == GL_DEPTH_TEST) return depth; if (value == GL_STENCIL_TEST) return stencil; if (value == GL_SCISSOR_TEST) return scissor; return value == 0x8DB9 ? srgb : GL_FALSE; }
void glBlendFuncSeparate(GLenum source_rgb, GLenum destination_rgb, GLenum source_alpha, GLenum destination_alpha)
{ blend_source_rgb = (GLint)source_rgb; blend_destination_rgb = (GLint)destination_rgb; blend_source_alpha = (GLint)source_alpha; blend_destination_alpha = (GLint)destination_alpha; }
void glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha) { blend_equation_rgb = (GLint)mode_rgb; blend_equation_alpha = (GLint)mode_alpha; }
void glActiveTexture(GLenum value) { active_texture = (GLint)value; }
void glBindTexture(GLenum target, GLuint value) { if (target == GL_TEXTURE_2D && active_texture == 0x84C0) texture = (GLint)value; }
void glBindSampler(GLuint unit, GLuint value) { if (!unit) sampler = (GLint)value; }
void glBindFramebuffer(GLenum target, GLuint value) { if (target == 0x8CA9) framebuffer = (GLint)value; }
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{ viewport[0] = x; viewport[1] = y; viewport[2] = width; viewport[3] = height; }
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{ scissor_box[0] = x; scissor_box[1] = y; scissor_box[2] = width; scissor_box[3] = height; }
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{ color_mask[0] = red; color_mask[1] = green; color_mask[2] = blue; color_mask[3] = alpha; }
void glBegin(GLenum value) { (void)value; }
void glEnd(void) {}
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{ (void)red; (void)green; (void)blue; (void)alpha; }
void glVertex2f(GLfloat x, GLfloat y) { (void)x; (void)y; ++vertices; }
GLuint glCreateShader(GLenum type) { return (GLuint)type; }
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *source, const GLint *length)
{
    (void)count; (void)length;
    if (shader == 0x8B31U && source && *source && strstr(*source, "position.y/viewport.y*-2.0+1.0"))
        native_vertex_shader = GL_TRUE;
}
void glCompileShader(GLuint shader) { (void)shader; }
void glGetShaderiv(GLuint shader, GLenum parameter, GLint *value)
{ (void)shader; (void)parameter; *value = 1; }
void glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *info)
{ (void)shader; if (length) *length = 0; if (size && info) info[0] = '\0'; }
void glDeleteShader(GLuint shader) { (void)shader; }
GLuint glCreateProgram(void) { return 1; }
void glAttachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{ (void)program; (void)index; (void)name; }
void glLinkProgram(GLuint program) { (void)program; }
void glGetProgramiv(GLuint program, GLenum parameter, GLint *value)
{ (void)program; (void)parameter; *value = 1; }
void glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *info)
{ (void)program; if (length) *length = 0; if (size && info) info[0] = '\0'; }
void glDeleteProgram(GLuint program) { (void)program; }
void glUseProgram(GLuint value) { program = (GLint)value; }
GLint glGetUniformLocation(GLuint program, const GLchar *name) { (void)program; (void)name; return 0; }
void glUniform2f(GLint location, GLfloat x, GLfloat y) { (void)location; (void)x; (void)y; }
void glGenVertexArrays(GLsizei count, GLuint *arrays) { while (count--) *arrays++ = 2; }
void glBindVertexArray(GLuint value) { vao = (GLint)value; }
void glGenBuffers(GLsizei count, GLuint *buffers) { while (count--) *buffers++ = 3; }
void glBindBuffer(GLenum target, GLuint value) { if (target == 0x8892) array_buffer = (GLint)value; }
void glBufferData(GLenum target, ptrdiff_t size, const void *data, GLenum usage)
{ (void)target; (void)size; (void)data; (void)usage; }
void glEnableVertexAttribArray(GLuint index) { (void)index; }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{ (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{ (void)mode; (void)first; vertices += (unsigned int)count; }
