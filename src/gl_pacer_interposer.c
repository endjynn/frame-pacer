#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include "hud_fps.h"
#include "hud_metrics.h"
#include "hud_text.h"
#include "hud_vertices.h"
#include "log_retention.h"
#include "pacer_clock.h"
#include "pacer_limit.h"
#include "thread_cpu_quota.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_LIMIT (UINT64_C(64) * 1024 * 1024)
#define FRAME_PACER_GL_ARRAY_BUFFER 0x8892U
#define FRAME_PACER_GL_ARRAY_BUFFER_BINDING 0x8894U
#define FRAME_PACER_GL_STATIC_DRAW 0x88E4U
#define FRAME_PACER_GL_DRAW_FRAMEBUFFER 0x8CA9U
#define FRAME_PACER_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6U
#define FRAME_PACER_GL_ACTIVE_TEXTURE 0x84E0U
#define FRAME_PACER_GL_TEXTURE_BINDING_2D 0x8069U
#define FRAME_PACER_GL_TEXTURE0 0x84C0U
#define FRAME_PACER_GL_SAMPLER_BINDING 0x8919U
#define FRAME_PACER_GL_SCISSOR_BOX 0x0C10U
#define FRAME_PACER_GL_BLEND_SRC_RGB 0x80C9U
#define FRAME_PACER_GL_BLEND_DST_RGB 0x80C8U
#define FRAME_PACER_GL_BLEND_SRC_ALPHA 0x80CBU
#define FRAME_PACER_GL_BLEND_DST_ALPHA 0x80CAU
#define FRAME_PACER_GL_BLEND_EQUATION_RGB 0x8009U
#define FRAME_PACER_GL_BLEND_EQUATION_ALPHA 0x883DU
#define FRAME_PACER_GL_FUNC_ADD 0x8006U
#define FRAME_PACER_GL_FRAMEBUFFER_SRGB 0x8DB9U
#define FRAME_PACER_GL_COLOR_WRITEMASK 0x0C23U
#define FRAME_PACER_GL_VERTEX_ARRAY_BINDING 0x85B5U
#define FRAME_PACER_GL_CURRENT_PROGRAM 0x8B8DU
#define FRAME_PACER_GL_VERTEX_SHADER 0x8B31U
#define FRAME_PACER_GL_FRAGMENT_SHADER 0x8B30U
#define FRAME_PACER_GL_COMPILE_STATUS 0x8B81U
#define FRAME_PACER_GL_LINK_STATUS 0x8B82U

#if __SIZEOF_POINTER__ == 8
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.2.5"
#else
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.0"
#endif

typedef void (*frame_pacer_glx_swap_fn)(Display *, GLXDrawable);
typedef EGLBoolean (*frame_pacer_egl_swap_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*frame_pacer_egl_swap_damage_fn)(EGLDisplay, EGLSurface,
                                                      const EGLint *, EGLint);
typedef EGLContext (*frame_pacer_egl_current_context_fn)(void);
typedef EGLBoolean (*frame_pacer_egl_query_surface_fn)(EGLDisplay, EGLSurface,
                                                        EGLint, EGLint *);
typedef GLXContext (*frame_pacer_glx_current_context_fn)(void);
typedef void (*frame_pacer_glx_query_drawable_fn)(Display *, GLXDrawable, int, unsigned int *);
typedef const GLubyte *(*frame_pacer_gl_get_string_fn)(GLenum);
typedef void (*frame_pacer_gl_get_integer_fn)(GLenum, GLint *);
typedef void (*frame_pacer_gl_disable_fn)(GLenum);
typedef void (*frame_pacer_gl_enable_fn)(GLenum);
typedef void (*frame_pacer_gl_blend_func_separate_fn)(GLenum, GLenum, GLenum, GLenum);
typedef void (*frame_pacer_gl_blend_equation_separate_fn)(GLenum, GLenum);
typedef GLboolean (*frame_pacer_gl_is_enabled_fn)(GLenum);
typedef void (*frame_pacer_gl_active_texture_fn)(GLenum);
typedef void (*frame_pacer_gl_bind_texture_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_bind_sampler_fn)(GLuint, GLuint);
typedef void (*frame_pacer_gl_bind_framebuffer_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*frame_pacer_gl_scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*frame_pacer_gl_color_mask_fn)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef GLuint (*frame_pacer_gl_create_shader_fn)(GLenum);
typedef void (*frame_pacer_gl_shader_source_fn)(GLuint, GLsizei,
                                                const GLchar *const *,
                                                const GLint *);
typedef void (*frame_pacer_gl_compile_shader_fn)(GLuint);
typedef void (*frame_pacer_gl_get_shader_iv_fn)(GLuint, GLenum, GLint *);
typedef void (*frame_pacer_gl_get_shader_info_log_fn)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*frame_pacer_gl_delete_shader_fn)(GLuint);
typedef GLuint (*frame_pacer_gl_create_program_fn)(void);
typedef void (*frame_pacer_gl_attach_shader_fn)(GLuint, GLuint);
typedef void (*frame_pacer_gl_bind_attrib_location_fn)(GLuint, GLuint, const GLchar *);
typedef void (*frame_pacer_gl_link_program_fn)(GLuint);
typedef void (*frame_pacer_gl_get_program_iv_fn)(GLuint, GLenum, GLint *);
typedef void (*frame_pacer_gl_get_program_info_log_fn)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*frame_pacer_gl_delete_program_fn)(GLuint);
typedef void (*frame_pacer_gl_use_program_fn)(GLuint);
typedef GLint (*frame_pacer_gl_get_uniform_location_fn)(GLuint, const GLchar *);
typedef void (*frame_pacer_gl_uniform_2f_fn)(GLint, GLfloat, GLfloat);
typedef void (*frame_pacer_gl_gen_vertex_arrays_fn)(GLsizei, GLuint *);
typedef void (*frame_pacer_gl_bind_vertex_array_fn)(GLuint);
typedef void (*frame_pacer_gl_gen_buffers_fn)(GLsizei, GLuint *);
typedef void (*frame_pacer_gl_bind_buffer_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_buffer_data_fn)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*frame_pacer_gl_enable_vertex_attrib_array_fn)(GLuint);
typedef void (*frame_pacer_gl_vertex_attrib_pointer_fn)(GLuint, GLint, GLenum,
                                                        GLboolean, GLsizei,
                                                        const void *);
typedef void (*frame_pacer_gl_draw_arrays_fn)(GLenum, GLint, GLsizei);

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_once_t metrics_once = PTHREAD_ONCE_INIT;
static void logmsg(const char *format, ...);
static struct frame_pacer_clock clock_;
static struct frame_pacer_limit limit_;
static struct frame_pacer_thread_cpu_quota thread_cpu_quota_;
static void thread_cpu_quota_log(const char *message) { logmsg("%s", message); }
static struct frame_pacer_fps_tracker fps_;
static struct frame_pacer_metrics metrics_;
static frame_pacer_glx_swap_fn next_glx_swap;
static frame_pacer_egl_swap_fn next_egl_swap;
static frame_pacer_egl_swap_damage_fn next_egl_swap_damage_khr;
static frame_pacer_egl_swap_damage_fn next_egl_swap_damage_ext;
static void *(*real_dlsym)(void *, const char *);
static __GLXextFuncPtr (*next_glx_get_proc)(const GLubyte *);
static __eglMustCastToProperFunctionPointerType (*next_egl_get_proc)(const char *);
static frame_pacer_glx_current_context_fn next_glx_current_context;
static frame_pacer_glx_query_drawable_fn next_glx_query_drawable;
static frame_pacer_egl_current_context_fn next_egl_current_context;
static frame_pacer_egl_query_surface_fn next_egl_query_surface;
static frame_pacer_gl_get_string_fn gl_get_string;
static frame_pacer_gl_get_integer_fn gl_get_integer;
static frame_pacer_gl_disable_fn gl_disable;
static frame_pacer_gl_enable_fn gl_enable;
static frame_pacer_gl_blend_func_separate_fn gl_blend_func_separate;
static frame_pacer_gl_blend_equation_separate_fn gl_blend_equation_separate;
static frame_pacer_gl_is_enabled_fn gl_is_enabled;
static frame_pacer_gl_active_texture_fn gl_active_texture;
static frame_pacer_gl_bind_texture_fn gl_bind_texture;
static frame_pacer_gl_bind_sampler_fn gl_bind_sampler;
static frame_pacer_gl_bind_framebuffer_fn gl_bind_framebuffer;
static frame_pacer_gl_viewport_fn gl_viewport;
static frame_pacer_gl_scissor_fn gl_scissor;
static frame_pacer_gl_color_mask_fn gl_color_mask;
static frame_pacer_gl_create_shader_fn gl_create_shader;
static frame_pacer_gl_shader_source_fn gl_shader_source;
static frame_pacer_gl_compile_shader_fn gl_compile_shader;
static frame_pacer_gl_get_shader_iv_fn gl_get_shader_iv;
static frame_pacer_gl_get_shader_info_log_fn gl_get_shader_info_log;
static frame_pacer_gl_delete_shader_fn gl_delete_shader;
static frame_pacer_gl_create_program_fn gl_create_program;
static frame_pacer_gl_attach_shader_fn gl_attach_shader;
static frame_pacer_gl_bind_attrib_location_fn gl_bind_attrib_location;
static frame_pacer_gl_link_program_fn gl_link_program;
static frame_pacer_gl_get_program_iv_fn gl_get_program_iv;
static frame_pacer_gl_get_program_info_log_fn gl_get_program_info_log;
static frame_pacer_gl_delete_program_fn gl_delete_program;
static frame_pacer_gl_use_program_fn gl_use_program;
static frame_pacer_gl_get_uniform_location_fn gl_get_uniform_location;
static frame_pacer_gl_uniform_2f_fn gl_uniform_2f;
static frame_pacer_gl_gen_vertex_arrays_fn gl_gen_vertex_arrays;
static frame_pacer_gl_bind_vertex_array_fn gl_bind_vertex_array;
static frame_pacer_gl_gen_buffers_fn gl_gen_buffers;
static frame_pacer_gl_bind_buffer_fn gl_bind_buffer;
static frame_pacer_gl_buffer_data_fn gl_buffer_data;
static frame_pacer_gl_enable_vertex_attrib_array_fn gl_enable_vertex_attrib_array;
static frame_pacer_gl_vertex_attrib_pointer_fn gl_vertex_attrib_pointer;
static frame_pacer_gl_draw_arrays_fn gl_draw_arrays;
static int log_fd = -1;
static uint64_t log_bytes, swaps;
static unsigned int resolver_requests;
static bool log_capped;
static bool hud_available;
static bool backend_initialized;
static bool metrics_initialized;
static void *hud_context;
static GLuint hud_program, hud_vao, hud_vbo;
static GLint hud_viewport_uniform;

struct frame_pacer_gl_state {
    GLint framebuffer;
    GLint active_texture;
    GLint texture;
    GLint sampler;
    GLint program;
    GLint vao;
    GLint array_buffer;
    GLint viewport[4];
    GLint scissor_box[4];
    GLint blend_src_rgb;
    GLint blend_dst_rgb;
    GLint blend_src_alpha;
    GLint blend_dst_alpha;
    GLint blend_equation_rgb;
    GLint blend_equation_alpha;
    GLint color_mask[4];
    GLboolean blend, cull, depth, stencil, scissor, srgb;
};

static uint64_t now_ns(void *unused)
{
    struct timespec time;
    (void)unused;
    return clock_gettime(CLOCK_MONOTONIC, &time) ? 0 :
        (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static int sleep_until(void *unused, uint64_t deadline)
{
    struct timespec time = {
        .tv_sec = (time_t)(deadline / UINT64_C(1000000000)),
        .tv_nsec = (long)(deadline % UINT64_C(1000000000)),
    };
    (void)unused;
    return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, 0);
}

static void logmsg(const char *format, ...)
{
    char buffer[512];
    va_list arguments;
    int length;
    size_t bytes;
    ssize_t written;

    if (log_fd < 0 || log_capped) return;
    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0) return;
    bytes = (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1;
    if (log_bytes + bytes + 48 > LOG_LIMIT) {
        static const char cap[] = "frame-pacer: log cap reached; pacing continues\n";
        ssize_t ignored = write(log_fd, cap, sizeof(cap) - 1);
        (void)ignored;
        log_capped = true;
        return;
    }
    written = write(log_fd, buffer, bytes);
    if (written > 0) log_bytes += (uint64_t)written;
}

static void init_log(void)
{
    const char *state = getenv("XDG_STATE_HOME");
    const char *home;
    char root[1024], directory[1100], path[1200];

    if (!frame_pacer_log_enabled())
        return;
    if (!state || !*state) {
        home = getenv("HOME");
        if (!home || !*home ||
            snprintf(root, sizeof(root), "%s/.local/state", home) >=
                (int)sizeof(root))
            return;
        if (mkdir(root, 0700) && errno != EEXIST)
            return;
        state = root;
    }
    if (snprintf(directory, sizeof(directory), "%s/frame-pacer", state) >=
        (int)sizeof(directory))
        return;
    if (mkdir(directory, 0700) && errno != EEXIST)
        return;
    if (snprintf(path, sizeof(path), "%s/frame-pacer-gl-%ld.log", directory,
                 (long)getpid()) >= (int)sizeof(path))
        return;
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (log_fd >= 0) {
        struct stat status;

        if (fstat(log_fd, &status) || !S_ISREG(status.st_mode) || status.st_uid != geteuid()) {
            (void)close(log_fd);
            log_fd = -1;
        }
    }
    if (log_fd >= 0)
        frame_pacer_log_retention_prune(directory, "frame-pacer-gl-");
}

static void gl_function(void *library, const char *name, void *function, size_t size)
{
    void *symbol = real_dlsym ? real_dlsym(library, name) : 0;

    if (!symbol && next_glx_get_proc) {
        __GLXextFuncPtr extension = next_glx_get_proc((const GLubyte *)name);

        _Static_assert(sizeof(symbol) == sizeof(extension),
                       "unsupported function pointer representation");
        memcpy(&symbol, &extension, sizeof(symbol));
    }
    if (!symbol && next_egl_get_proc) {
        __eglMustCastToProperFunctionPointerType extension = next_egl_get_proc(name);

        _Static_assert(sizeof(symbol) == sizeof(extension),
                       "unsupported function pointer representation");
        memcpy(&symbol, &extension, sizeof(symbol));
    }
    memcpy(function, &symbol, size);
}

static bool init_hud(void *gl)
{
    gl_function(gl, "glXGetCurrentContext", &next_glx_current_context,
                sizeof(next_glx_current_context));
    gl_function(gl, "glXQueryDrawable", &next_glx_query_drawable,
                sizeof(next_glx_query_drawable));
    gl_function(gl, "eglGetCurrentContext", &next_egl_current_context,
                sizeof(next_egl_current_context));
    gl_function(gl, "eglQuerySurface", &next_egl_query_surface,
                sizeof(next_egl_query_surface));
    gl_function(gl, "glGetString", &gl_get_string, sizeof(gl_get_string));
    gl_function(gl, "glGetIntegerv", &gl_get_integer, sizeof(gl_get_integer));
    gl_function(gl, "glDisable", &gl_disable, sizeof(gl_disable));
    gl_function(gl, "glEnable", &gl_enable, sizeof(gl_enable));
    gl_function(gl, "glBlendFuncSeparate", &gl_blend_func_separate, sizeof(gl_blend_func_separate));
    gl_function(gl, "glBlendEquationSeparate", &gl_blend_equation_separate,
                sizeof(gl_blend_equation_separate));
    gl_function(gl, "glIsEnabled", &gl_is_enabled, sizeof(gl_is_enabled));
    gl_function(gl, "glActiveTexture", &gl_active_texture, sizeof(gl_active_texture));
    gl_function(gl, "glBindTexture", &gl_bind_texture, sizeof(gl_bind_texture));
    gl_function(gl, "glBindSampler", &gl_bind_sampler, sizeof(gl_bind_sampler));
    gl_function(gl, "glBindFramebuffer", &gl_bind_framebuffer, sizeof(gl_bind_framebuffer));
    gl_function(gl, "glViewport", &gl_viewport, sizeof(gl_viewport));
    gl_function(gl, "glScissor", &gl_scissor, sizeof(gl_scissor));
    gl_function(gl, "glColorMask", &gl_color_mask, sizeof(gl_color_mask));
    gl_function(gl, "glCreateShader", &gl_create_shader, sizeof(gl_create_shader));
    gl_function(gl, "glShaderSource", &gl_shader_source, sizeof(gl_shader_source));
    gl_function(gl, "glCompileShader", &gl_compile_shader, sizeof(gl_compile_shader));
    gl_function(gl, "glGetShaderiv", &gl_get_shader_iv, sizeof(gl_get_shader_iv));
    gl_function(gl, "glGetShaderInfoLog", &gl_get_shader_info_log, sizeof(gl_get_shader_info_log));
    gl_function(gl, "glDeleteShader", &gl_delete_shader, sizeof(gl_delete_shader));
    gl_function(gl, "glCreateProgram", &gl_create_program, sizeof(gl_create_program));
    gl_function(gl, "glAttachShader", &gl_attach_shader, sizeof(gl_attach_shader));
    gl_function(gl, "glBindAttribLocation", &gl_bind_attrib_location,
                sizeof(gl_bind_attrib_location));
    gl_function(gl, "glLinkProgram", &gl_link_program, sizeof(gl_link_program));
    gl_function(gl, "glGetProgramiv", &gl_get_program_iv, sizeof(gl_get_program_iv));
    gl_function(gl, "glGetProgramInfoLog", &gl_get_program_info_log,
                sizeof(gl_get_program_info_log));
    gl_function(gl, "glDeleteProgram", &gl_delete_program, sizeof(gl_delete_program));
    gl_function(gl, "glUseProgram", &gl_use_program, sizeof(gl_use_program));
    gl_function(gl, "glGetUniformLocation", &gl_get_uniform_location,
                sizeof(gl_get_uniform_location));
    gl_function(gl, "glUniform2f", &gl_uniform_2f, sizeof(gl_uniform_2f));
    gl_function(gl, "glGenVertexArrays", &gl_gen_vertex_arrays, sizeof(gl_gen_vertex_arrays));
    gl_function(gl, "glBindVertexArray", &gl_bind_vertex_array, sizeof(gl_bind_vertex_array));
    gl_function(gl, "glGenBuffers", &gl_gen_buffers, sizeof(gl_gen_buffers));
    gl_function(gl, "glBindBuffer", &gl_bind_buffer, sizeof(gl_bind_buffer));
    gl_function(gl, "glBufferData", &gl_buffer_data, sizeof(gl_buffer_data));
    gl_function(gl, "glEnableVertexAttribArray", &gl_enable_vertex_attrib_array,
                sizeof(gl_enable_vertex_attrib_array));
    gl_function(gl, "glVertexAttribPointer", &gl_vertex_attrib_pointer,
                sizeof(gl_vertex_attrib_pointer));
    gl_function(gl, "glDrawArrays", &gl_draw_arrays, sizeof(gl_draw_arrays));
    return (next_glx_current_context || next_egl_current_context) && gl_get_string &&
           gl_get_integer && gl_disable && gl_enable && gl_blend_func_separate &&
           gl_blend_equation_separate && gl_is_enabled && gl_active_texture &&
           gl_bind_texture && gl_bind_sampler && gl_bind_framebuffer &&
           gl_viewport && gl_scissor && gl_color_mask && gl_create_shader &&
           gl_shader_source && gl_compile_shader && gl_get_shader_iv &&
           gl_get_shader_info_log && gl_delete_shader && gl_create_program &&
           gl_attach_shader && gl_bind_attrib_location && gl_link_program &&
           gl_get_program_iv && gl_get_program_info_log && gl_delete_program &&
           gl_use_program && gl_get_uniform_location && gl_uniform_2f &&
           gl_gen_vertex_arrays && gl_bind_vertex_array && gl_gen_buffers &&
           gl_bind_buffer && gl_buffer_data && gl_enable_vertex_attrib_array &&
           gl_vertex_attrib_pointer && gl_draw_arrays;
}

static void init(void)
{
    void *dlsym_symbol;

    frame_pacer_clock_init(&clock_);
    frame_pacer_limit_init(&limit_);
    frame_pacer_thread_cpu_quota_init(&thread_cpu_quota_);
    frame_pacer_thread_cpu_quota_set_logger(&thread_cpu_quota_, thread_cpu_quota_log);
    frame_pacer_fps_init(&fps_);
    init_log();
    {
        void *libc = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);

        dlsym_symbol = libc ? dlvsym(libc, "dlsym", FRAME_PACER_GLIBC_DLSYM_VERSION) : 0;
    }
    _Static_assert(sizeof(real_dlsym) == sizeof(dlsym_symbol),
                   "unsupported function pointer representation");
    memcpy(&real_dlsym, &dlsym_symbol, sizeof(real_dlsym));
    if (real_dlsym) {
        void *gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
        void *egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);

        if (gl) {
            *(void **)(&next_glx_swap) = real_dlsym(gl, "glXSwapBuffers");
            *(void **)(&next_egl_swap) = real_dlsym(gl, "eglSwapBuffers");
            *(void **)(&next_egl_swap_damage_khr) =
                real_dlsym(gl, "eglSwapBuffersWithDamageKHR");
            *(void **)(&next_egl_swap_damage_ext) =
                real_dlsym(gl, "eglSwapBuffersWithDamageEXT");
            *(void **)(&next_glx_get_proc) = real_dlsym(gl, "glXGetProcAddress");
            *(void **)(&next_egl_get_proc) = real_dlsym(gl, "eglGetProcAddress");
        }
        if (egl) {
            if (!next_egl_swap)
                *(void **)(&next_egl_swap) = real_dlsym(egl, "eglSwapBuffers");
            if (!next_egl_swap_damage_khr)
                *(void **)(&next_egl_swap_damage_khr) =
                    real_dlsym(egl, "eglSwapBuffersWithDamageKHR");
            if (!next_egl_swap_damage_ext)
                *(void **)(&next_egl_swap_damage_ext) =
                    real_dlsym(egl, "eglSwapBuffersWithDamageEXT");
            if (!next_egl_get_proc)
                *(void **)(&next_egl_get_proc) = real_dlsym(egl, "eglGetProcAddress");
        }
        if (!next_glx_swap) *(void **)(&next_glx_swap) = real_dlsym(RTLD_NEXT, "glXSwapBuffers");
        if (!next_egl_swap) *(void **)(&next_egl_swap) = real_dlsym(RTLD_NEXT, "eglSwapBuffers");
        if (!next_egl_swap_damage_khr)
            *(void **)(&next_egl_swap_damage_khr) =
                real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageKHR");
        if (!next_egl_swap_damage_ext)
            *(void **)(&next_egl_swap_damage_ext) =
                real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageEXT");
        if (!next_glx_get_proc)
            *(void **)(&next_glx_get_proc) =
                real_dlsym(RTLD_NEXT, "glXGetProcAddress");
        if (!next_egl_get_proc)
            *(void **)(&next_egl_get_proc) =
                real_dlsym(RTLD_NEXT, "eglGetProcAddress");
        hud_available = init_hud(gl ? gl : egl);
    }
    logmsg("frame-pacer: GL interposer init pid=%ld arch=%zu dlsym=%d glx=%d egl=%d hud=%d\n",
           (long)getpid(), sizeof(void *) * 8, real_dlsym != 0, next_glx_swap != 0,
           next_egl_swap != 0, hud_available);
    backend_initialized = true;
}

static void init_metrics(void)
{
    frame_pacer_metrics_init(&metrics_, 0, (unsigned int)getpid());
    metrics_initialized = true;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count);
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count);

static void *swap_symbol(const char *name)
{
    void *address = 0;
    frame_pacer_glx_swap_fn glx_swap;
    frame_pacer_egl_swap_fn egl_swap;

    if (!name) return 0;
    if (!strcmp(name, "glXSwapBuffers")) {
        glx_swap = glXSwapBuffers;
        _Static_assert(sizeof(address) == sizeof(glx_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &glx_swap, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffers")) {
        egl_swap = eglSwapBuffers;
        _Static_assert(sizeof(address) == sizeof(egl_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_swap, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffersWithDamageKHR")) {
        frame_pacer_egl_swap_damage_fn egl_swap = eglSwapBuffersWithDamageKHR;

        _Static_assert(sizeof(address) == sizeof(egl_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_swap, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffersWithDamageEXT")) {
        frame_pacer_egl_swap_damage_fn egl_swap = eglSwapBuffersWithDamageEXT;

        _Static_assert(sizeof(address) == sizeof(egl_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_swap, sizeof(address));
    }
    return address;
}

static void pace(const char *api)
{
    struct frame_pacer_decision decision;
    bool changed;
    uint32_t fps = frame_pacer_limit_poll(&limit_, now_ns(0), &changed);

    if (changed) logmsg("frame-pacer: GL FPS limit changed to %u\n", fps);
    frame_pacer_clock_wait(&clock_, fps, now_ns, sleep_until, 0, &decision);
    ++swaps;
    logmsg("frame-pacer: %s first=%d missed=%d now=%" PRIu64
           " deadline=%" PRIu64 " eintr=%u cap=%u\n", api, decision.first,
           decision.missed, decision.observed_ns, decision.deadline_ns,
           decision.interruptions, fps);
}

static bool compile_shader(GLenum type, const char *source, GLuint *shader)
{
    GLint ok = 0;
    GLchar info[256] = {0};

    *shader = gl_create_shader(type);
    if (!*shader) return false;
    gl_shader_source(*shader, 1, (const GLchar *const *)&source, 0);
    gl_compile_shader(*shader);
    gl_get_shader_iv(*shader, FRAME_PACER_GL_COMPILE_STATUS, &ok);
    if (ok) return true;
    gl_get_shader_info_log(*shader, (GLsizei)sizeof(info) - 1, 0, info);
    logmsg("frame-pacer: GL shader compile failed type=0x%x info=%s\n", type, info);
    gl_delete_shader(*shader);
    *shader = 0;
    return false;
}

static bool process_uses_wined3d(void)
{
    static const char needle[] = "wined3d.dll";
    char buffer[4096 + sizeof(needle)];
    size_t retained = 0;
    int maps;
    ssize_t read_bytes;

    maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (maps < 0) return false;
    while ((read_bytes = read(maps, buffer + retained, 4096)) > 0) {
        size_t bytes = retained + (size_t)read_bytes;

        buffer[bytes] = '\0';
        if (strstr(buffer, needle)) {
            (void)close(maps);
            return true;
        }
        retained = bytes < sizeof(needle) - 1 ? bytes : sizeof(needle) - 1;
        memmove(buffer, buffer + bytes - retained, retained);
    }
    (void)close(maps);
    return false;
}

static bool create_hud_resources(void *context)
{
    static const char native_vertex_source[] =
        "#version 130\n"
        "in vec2 position; in vec4 color; out vec4 pass_color; uniform vec2 viewport;\n"
        "void main(){ gl_Position=vec4(position.x/viewport.x*2.0-1.0,"
        "position.y/viewport.y*-2.0+1.0,0.0,1.0); pass_color=color; }\n";
    static const char wined3d_vertex_source[] =
        "#version 130\n"
        "in vec2 position; in vec4 color; out vec4 pass_color; uniform vec2 viewport;\n"
        "void main(){ gl_Position=vec4(position.x/viewport.x*2.0-1.0,"
        "position.y/viewport.y*2.0-1.0,0.0,1.0); pass_color=color; }\n";
    static const char fragment_source[] =
        "#version 130\n"
        "in vec4 pass_color; out vec4 output_color; void main(){ output_color=pass_color; }\n";
    GLuint vertex = 0, fragment = 0;
    GLint ok = 0;
    GLchar info[256] = {0};
    const GLubyte *version;
    bool wined3d;

    if (hud_context == context && hud_program && hud_vao && hud_vbo) return true;
    hud_context = context;
    hud_program = hud_vao = hud_vbo = 0;
    version = gl_get_string(GL_VERSION);
    logmsg("frame-pacer: GL HUD context version=%s\n",
           version ? (const char *)version : "unavailable");
    wined3d = process_uses_wined3d();
    logmsg("frame-pacer: GL HUD coordinates=%s\n", wined3d ? "wined3d" : "native");
    if (!compile_shader(FRAME_PACER_GL_VERTEX_SHADER,
                        wined3d ? wined3d_vertex_source : native_vertex_source, &vertex) ||
        !compile_shader(FRAME_PACER_GL_FRAGMENT_SHADER, fragment_source, &fragment)) goto fail;
    hud_program = gl_create_program();
    if (!hud_program) goto fail;
    gl_attach_shader(hud_program, vertex);
    gl_attach_shader(hud_program, fragment);
    gl_bind_attrib_location(hud_program, 0, "position");
    gl_bind_attrib_location(hud_program, 1, "color");
    gl_link_program(hud_program);
    gl_get_program_iv(hud_program, FRAME_PACER_GL_LINK_STATUS, &ok);
    gl_delete_shader(vertex);
    gl_delete_shader(fragment);
    if (!ok) {
        gl_get_program_info_log(hud_program, (GLsizei)sizeof(info) - 1, 0, info);
        logmsg("frame-pacer: GL program link failed info=%s\n", info);
        goto fail;
    }
    hud_viewport_uniform = gl_get_uniform_location(hud_program, "viewport");
    if (hud_viewport_uniform < 0) goto fail;
    gl_gen_vertex_arrays(1, &hud_vao);
    gl_gen_buffers(1, &hud_vbo);
    if (!hud_vao || !hud_vbo) goto fail;
    gl_bind_vertex_array(hud_vao);
    gl_bind_buffer(FRAME_PACER_GL_ARRAY_BUFFER, hud_vbo);
    gl_enable_vertex_attrib_array(0);
    gl_vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE,
                             (GLsizei)sizeof(struct frame_pacer_hud_vertex), 0);
    gl_enable_vertex_attrib_array(1);
    gl_vertex_attrib_pointer(1, 4, GL_FLOAT, GL_FALSE,
                             (GLsizei)sizeof(struct frame_pacer_hud_vertex),
                             (const void *)(2 * sizeof(float)));
    return true;
fail:
    logmsg("frame-pacer: GL HUD resources unavailable\n");
    if (vertex) gl_delete_shader(vertex);
    if (fragment) gl_delete_shader(fragment);
    if (hud_program) gl_delete_program(hud_program);
    hud_program = hud_vao = hud_vbo = 0;
    return false;
}

static void save_gl_state(struct frame_pacer_gl_state *state)
{
    gl_get_integer(FRAME_PACER_GL_DRAW_FRAMEBUFFER_BINDING, &state->framebuffer);
    gl_get_integer(FRAME_PACER_GL_ACTIVE_TEXTURE, &state->active_texture);
    gl_active_texture(FRAME_PACER_GL_TEXTURE0);
    gl_get_integer(FRAME_PACER_GL_TEXTURE_BINDING_2D, &state->texture);
    gl_get_integer(FRAME_PACER_GL_SAMPLER_BINDING, &state->sampler);
    gl_get_integer(FRAME_PACER_GL_CURRENT_PROGRAM, &state->program);
    gl_get_integer(FRAME_PACER_GL_VERTEX_ARRAY_BINDING, &state->vao);
    gl_get_integer(FRAME_PACER_GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
    gl_get_integer(GL_VIEWPORT, state->viewport);
    gl_get_integer(FRAME_PACER_GL_SCISSOR_BOX, state->scissor_box);
    gl_get_integer(FRAME_PACER_GL_COLOR_WRITEMASK, state->color_mask);
    gl_get_integer(FRAME_PACER_GL_BLEND_SRC_RGB, &state->blend_src_rgb);
    gl_get_integer(FRAME_PACER_GL_BLEND_DST_RGB, &state->blend_dst_rgb);
    gl_get_integer(FRAME_PACER_GL_BLEND_SRC_ALPHA, &state->blend_src_alpha);
    gl_get_integer(FRAME_PACER_GL_BLEND_DST_ALPHA, &state->blend_dst_alpha);
    gl_get_integer(FRAME_PACER_GL_BLEND_EQUATION_RGB, &state->blend_equation_rgb);
    gl_get_integer(FRAME_PACER_GL_BLEND_EQUATION_ALPHA, &state->blend_equation_alpha);
    state->blend = gl_is_enabled(GL_BLEND);
    state->cull = gl_is_enabled(GL_CULL_FACE);
    state->depth = gl_is_enabled(GL_DEPTH_TEST);
    state->stencil = gl_is_enabled(GL_STENCIL_TEST);
    state->scissor = gl_is_enabled(GL_SCISSOR_TEST);
    state->srgb = gl_is_enabled(FRAME_PACER_GL_FRAMEBUFFER_SRGB);
}

static void restore_gl_state(const struct frame_pacer_gl_state *state)
{
    gl_use_program((GLuint)state->program);
    gl_active_texture(FRAME_PACER_GL_TEXTURE0);
    gl_bind_texture(GL_TEXTURE_2D, (GLuint)state->texture);
    gl_bind_sampler(0, (GLuint)state->sampler);
    gl_active_texture((GLenum)state->active_texture);
    gl_bind_vertex_array((GLuint)state->vao);
    gl_bind_buffer(FRAME_PACER_GL_ARRAY_BUFFER, (GLuint)state->array_buffer);
    gl_blend_equation_separate((GLenum)state->blend_equation_rgb,
                               (GLenum)state->blend_equation_alpha);
    gl_blend_func_separate((GLenum)state->blend_src_rgb, (GLenum)state->blend_dst_rgb,
                           (GLenum)state->blend_src_alpha, (GLenum)state->blend_dst_alpha);
    if (state->blend) gl_enable(GL_BLEND); else gl_disable(GL_BLEND);
    if (state->cull) gl_enable(GL_CULL_FACE); else gl_disable(GL_CULL_FACE);
    if (state->depth) gl_enable(GL_DEPTH_TEST); else gl_disable(GL_DEPTH_TEST);
    if (state->stencil) gl_enable(GL_STENCIL_TEST); else gl_disable(GL_STENCIL_TEST);
    if (state->scissor) gl_enable(GL_SCISSOR_TEST); else gl_disable(GL_SCISSOR_TEST);
    if (state->srgb)
        gl_enable(FRAME_PACER_GL_FRAMEBUFFER_SRGB);
    else
        gl_disable(FRAME_PACER_GL_FRAMEBUFFER_SRGB);
    gl_viewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
    gl_scissor(state->scissor_box[0], state->scissor_box[1],
               state->scissor_box[2], state->scissor_box[3]);
    gl_color_mask((GLboolean)state->color_mask[0], (GLboolean)state->color_mask[1],
                  (GLboolean)state->color_mask[2], (GLboolean)state->color_mask[3]);
    gl_bind_framebuffer(FRAME_PACER_GL_DRAW_FRAMEBUFFER, (GLuint)state->framebuffer);
}

static void render_hud(EGLDisplay egl_display, EGLSurface egl_surface,
                       Display *glx_display, GLXDrawable glx_drawable)
{
    struct frame_pacer_metrics_snapshot metrics;
    struct frame_pacer_hud_text text;
    struct frame_pacer_hud_vertices vertices;
    GLint viewport[4];
    unsigned int width = 0, height = 0;
    uint32_t fps, limit, thread_cpu_quota;
    bool changed;
    bool thread_cpu_quota_configured;

    void *context;
    struct frame_pacer_gl_state state;

    if (!hud_available) return;
    (void)frame_pacer_limit_poll(&limit_, now_ns(0), 0);
    if (!frame_pacer_limit_hud_enabled(&limit_)) return;
    context = next_glx_current_context ? (void *)next_glx_current_context() : 0;
    if (!context && next_egl_current_context)
        context = (void *)next_egl_current_context();
    if (!context) return;
    (void)pthread_once(&metrics_once, init_metrics);
    gl_get_integer(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    if (glx_display && next_glx_query_drawable) {
        next_glx_query_drawable(glx_display, glx_drawable, GLX_WIDTH, &width);
        next_glx_query_drawable(glx_display, glx_drawable, GLX_HEIGHT, &height);
    } else if (egl_display != EGL_NO_DISPLAY && egl_surface != EGL_NO_SURFACE &&
               next_egl_query_surface) {
        EGLint egl_width = 0, egl_height = 0;

        if (next_egl_query_surface(egl_display, egl_surface, EGL_WIDTH, &egl_width) &&
            egl_width > 0)
            width = (unsigned int)egl_width;
        if (next_egl_query_surface(egl_display, egl_surface, EGL_HEIGHT, &egl_height) &&
            egl_height > 0)
            height = (unsigned int)egl_height;
    }
    if (!width || !height) {
        width = (unsigned int)viewport[2];
        height = (unsigned int)viewport[3];
    }
    limit = frame_pacer_limit_poll(&limit_, now_ns(0), &changed);
    thread_cpu_quota = frame_pacer_limit_thread_cpu_quota(&limit_,
                                                           &thread_cpu_quota_configured);
    frame_pacer_thread_cpu_quota_publish(&thread_cpu_quota_, thread_cpu_quota_configured,
                                         thread_cpu_quota);
    (void)changed;
    frame_pacer_metrics_sample(&metrics_, &metrics);
    frame_pacer_hud_text_format(&text, &metrics, frame_pacer_fps_snapshot(&fps_, &fps), fps, limit,
                                thread_cpu_quota_configured,
                                frame_pacer_thread_cpu_quota_confirmed(&thread_cpu_quota_, 0),
                                thread_cpu_quota);
    save_gl_state(&state);
    if (!frame_pacer_hud_vertices_build(&vertices, &text)) {
        restore_gl_state(&state);
        return;
    }
    if (!create_hud_resources(context)) {
        restore_gl_state(&state);
        hud_available = false;
        return;
    }
    gl_bind_framebuffer(FRAME_PACER_GL_DRAW_FRAMEBUFFER, 0);
    gl_active_texture(FRAME_PACER_GL_TEXTURE0);
    gl_bind_sampler(0, 0);
    gl_viewport(0, 0, (GLsizei)width, (GLsizei)height);
    gl_scissor(0, 0, (GLsizei)width, (GLsizei)height);
    gl_disable(GL_DEPTH_TEST);
    gl_disable(GL_CULL_FACE);
    gl_disable(GL_STENCIL_TEST);
    gl_disable(FRAME_PACER_GL_FRAMEBUFFER_SRGB);
    gl_enable(GL_BLEND);
    gl_blend_equation_separate(FRAME_PACER_GL_FUNC_ADD, FRAME_PACER_GL_FUNC_ADD);
    gl_blend_func_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl_enable(GL_SCISSOR_TEST);
    gl_color_mask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_use_program(hud_program);
    gl_uniform_2f(hud_viewport_uniform, (GLfloat)width, (GLfloat)height);
    gl_bind_vertex_array(hud_vao);
    gl_bind_buffer(FRAME_PACER_GL_ARRAY_BUFFER, hud_vbo);
    gl_buffer_data(FRAME_PACER_GL_ARRAY_BUFFER,
                   (GLsizeiptr)(vertices.count * sizeof(vertices.data[0])),
                   vertices.data, FRAME_PACER_GL_STATIC_DRAW);
    gl_draw_arrays(GL_TRIANGLES, 0, (GLsizei)vertices.count);
    restore_gl_state(&state);
}

void glXSwapBuffers(Display *display, GLXDrawable drawable)
{
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next_glx_swap) return;
    render_hud(EGL_NO_DISPLAY, EGL_NO_SURFACE, display, drawable);
    pace("glXSwapBuffers");
    next_glx_swap(display, drawable);
    accepted = now_ns(0);
    if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    EGLBoolean result;
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next_egl_swap) return EGL_FALSE;
    render_hud(display, surface, 0, 0);
    pace("eglSwapBuffers");
    result = next_egl_swap(display, surface);
    accepted = now_ns(0);
    if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
    return result;
}

static EGLBoolean swap_damage(const char *api, frame_pacer_egl_swap_damage_fn next,
                              EGLDisplay display, EGLSurface surface,
                              const EGLint *rects, EGLint count)
{
    EGLBoolean result;
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next) return EGL_FALSE;
    render_hud(display, surface, 0, 0);
    pace(api);
    result = next(display, surface, rects, count);
    accepted = now_ns(0);
    if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
    return result;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    return swap_damage("eglSwapBuffersWithDamageKHR", next_egl_swap_damage_khr,
                       display, surface, rects, count);
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    return swap_damage("eglSwapBuffersWithDamageEXT", next_egl_swap_damage_ext,
                       display, surface, rects, count);
}

void *dlsym(void *handle, const char *name)
{
    void *replacement;

    (void)pthread_once(&init_once, init);
    if (resolver_requests++ < 64)
        logmsg("frame-pacer: dlsym requested %s\n", name);
    replacement = swap_symbol(name);
    if (replacement) {
        logmsg("frame-pacer: dlsym substituted %s\n", name);
        return replacement;
    }
    return real_dlsym ? real_dlsym(handle, name) : 0;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    void *replacement;
    __GLXextFuncPtr function = 0;

    (void)pthread_once(&init_once, init);
    if (name && resolver_requests++ < 64)
        logmsg("frame-pacer: glXGetProcAddress requested %s\n", name);
    replacement = swap_symbol((const char *)name);
    if (replacement) {
        memcpy(&function, &replacement, sizeof(function));
        logmsg("frame-pacer: glXGetProcAddress substituted %s\n", name);
        return function;
    }
    return next_glx_get_proc ? next_glx_get_proc(name) : 0;
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name)
{
    return glXGetProcAddress(name);
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    void *replacement;
    __eglMustCastToProperFunctionPointerType function = 0;

    (void)pthread_once(&init_once, init);
    if (name && resolver_requests++ < 64)
        logmsg("frame-pacer: eglGetProcAddress requested %s\n", name);
    replacement = swap_symbol(name);
    if (replacement) {
        memcpy(&function, &replacement, sizeof(function));
        logmsg("frame-pacer: eglGetProcAddress substituted %s\n", name);
        return function;
    }
    return next_egl_get_proc ? next_egl_get_proc(name) : 0;
}

static void __attribute__((destructor)) done(void)
{
    logmsg("frame-pacer: GL shutdown swaps=%" PRIu64 " log_bytes=%" PRIu64 "\n", swaps, log_bytes);
    if (log_fd >= 0) (void)close(log_fd);
    if (metrics_initialized) frame_pacer_metrics_destroy(&metrics_);
    if (backend_initialized) {
        frame_pacer_thread_cpu_quota_destroy(&thread_cpu_quota_);
        frame_pacer_fps_destroy(&fps_);
        frame_pacer_limit_destroy(&limit_);
        frame_pacer_clock_destroy(&clock_);
    }
}
