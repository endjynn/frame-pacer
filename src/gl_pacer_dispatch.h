#ifndef FRAME_PACER_GL_DISPATCH_H
#define FRAME_PACER_GL_DISPATCH_H

#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <stdbool.h>

typedef void (*frame_pacer_glx_swap_fn)(Display *, GLXDrawable);
typedef EGLBoolean (*frame_pacer_egl_swap_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*frame_pacer_egl_swap_damage_fn)(
    EGLDisplay, EGLSurface, const EGLint *, EGLint);
typedef EGLContext (*frame_pacer_egl_current_context_fn)(void);
typedef EGLBoolean (*frame_pacer_egl_query_surface_fn)(EGLDisplay, EGLSurface,
                                                        EGLint, EGLint *);
typedef EGLBoolean (*frame_pacer_egl_destroy_context_fn)(EGLDisplay,
                                                          EGLContext);
typedef EGLBoolean (*frame_pacer_egl_terminate_fn)(EGLDisplay);
typedef GLXContext (*frame_pacer_glx_current_context_fn)(void);
typedef void (*frame_pacer_glx_query_drawable_fn)(Display *, GLXDrawable, int,
                                                   unsigned int *);
typedef void (*frame_pacer_glx_destroy_context_fn)(Display *, GLXContext);
typedef const GLubyte *(*frame_pacer_gl_get_string_fn)(GLenum);
typedef void (*frame_pacer_gl_get_integer_fn)(GLenum, GLint *);
typedef void (*frame_pacer_gl_disable_fn)(GLenum);
typedef void (*frame_pacer_gl_enable_fn)(GLenum);
typedef void (*frame_pacer_gl_blend_func_separate_fn)(GLenum, GLenum, GLenum,
                                                       GLenum);
typedef void (*frame_pacer_gl_blend_equation_separate_fn)(GLenum, GLenum);
typedef GLboolean (*frame_pacer_gl_is_enabled_fn)(GLenum);
typedef void (*frame_pacer_gl_active_texture_fn)(GLenum);
typedef void (*frame_pacer_gl_bind_texture_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_bind_sampler_fn)(GLuint, GLuint);
typedef void (*frame_pacer_gl_bind_framebuffer_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*frame_pacer_gl_scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*frame_pacer_gl_color_mask_fn)(GLboolean, GLboolean, GLboolean,
                                              GLboolean);
typedef GLuint (*frame_pacer_gl_create_shader_fn)(GLenum);
typedef void (*frame_pacer_gl_shader_source_fn)(GLuint, GLsizei,
                                                 const GLchar *const *,
                                                 const GLint *);
typedef void (*frame_pacer_gl_compile_shader_fn)(GLuint);
typedef void (*frame_pacer_gl_get_shader_iv_fn)(GLuint, GLenum, GLint *);
typedef void (*frame_pacer_gl_get_shader_info_log_fn)(GLuint, GLsizei,
                                                       GLsizei *, GLchar *);
typedef void (*frame_pacer_gl_delete_shader_fn)(GLuint);
typedef GLuint (*frame_pacer_gl_create_program_fn)(void);
typedef void (*frame_pacer_gl_attach_shader_fn)(GLuint, GLuint);
typedef void (*frame_pacer_gl_bind_attrib_location_fn)(GLuint, GLuint,
                                                        const GLchar *);
typedef void (*frame_pacer_gl_link_program_fn)(GLuint);
typedef void (*frame_pacer_gl_get_program_iv_fn)(GLuint, GLenum, GLint *);
typedef void (*frame_pacer_gl_get_program_info_log_fn)(GLuint, GLsizei,
                                                        GLsizei *, GLchar *);
typedef void (*frame_pacer_gl_delete_program_fn)(GLuint);
typedef void (*frame_pacer_gl_use_program_fn)(GLuint);
typedef GLint (*frame_pacer_gl_get_uniform_location_fn)(GLuint,
                                                         const GLchar *);
typedef void (*frame_pacer_gl_uniform_2f_fn)(GLint, GLfloat, GLfloat);
typedef void (*frame_pacer_gl_gen_vertex_arrays_fn)(GLsizei, GLuint *);
typedef void (*frame_pacer_gl_delete_vertex_arrays_fn)(GLsizei,
                                                        const GLuint *);
typedef void (*frame_pacer_gl_bind_vertex_array_fn)(GLuint);
typedef void (*frame_pacer_gl_gen_buffers_fn)(GLsizei, GLuint *);
typedef void (*frame_pacer_gl_delete_buffers_fn)(GLsizei, const GLuint *);
typedef void (*frame_pacer_gl_bind_buffer_fn)(GLenum, GLuint);
typedef void (*frame_pacer_gl_buffer_data_fn)(GLenum, GLsizeiptr, const void *,
                                               GLenum);
typedef void (*frame_pacer_gl_enable_vertex_attrib_array_fn)(GLuint);
typedef void (*frame_pacer_gl_vertex_attrib_pointer_fn)(GLuint, GLint, GLenum,
                                                         GLboolean, GLsizei,
                                                         const void *);
typedef void (*frame_pacer_gl_draw_arrays_fn)(GLenum, GLint, GLsizei);

struct frame_pacer_gl_dispatch {
    void *libc_reference;
    void *gl_reference;
    void *egl_reference;
    void *(*real_dlsym)(void *, const char *);
    frame_pacer_glx_swap_fn next_glx_swap;
    frame_pacer_egl_swap_fn next_egl_swap;
    frame_pacer_egl_swap_damage_fn next_egl_swap_damage_khr;
    frame_pacer_egl_swap_damage_fn next_egl_swap_damage_ext;
    __GLXextFuncPtr (*next_glx_get_proc)(const GLubyte *);
    __eglMustCastToProperFunctionPointerType (*next_egl_get_proc)(const char *);
    frame_pacer_glx_current_context_fn next_glx_current_context;
    frame_pacer_glx_query_drawable_fn next_glx_query_drawable;
    frame_pacer_egl_current_context_fn next_egl_current_context;
    frame_pacer_egl_query_surface_fn next_egl_query_surface;
    frame_pacer_egl_destroy_context_fn next_egl_destroy_context;
    frame_pacer_egl_terminate_fn next_egl_terminate;
    frame_pacer_glx_destroy_context_fn next_glx_destroy_context;
    frame_pacer_gl_get_string_fn gl_get_string;
    frame_pacer_gl_get_integer_fn gl_get_integer;
    frame_pacer_gl_disable_fn gl_disable;
    frame_pacer_gl_enable_fn gl_enable;
    frame_pacer_gl_blend_func_separate_fn gl_blend_func_separate;
    frame_pacer_gl_blend_equation_separate_fn gl_blend_equation_separate;
    frame_pacer_gl_is_enabled_fn gl_is_enabled;
    frame_pacer_gl_active_texture_fn gl_active_texture;
    frame_pacer_gl_bind_texture_fn gl_bind_texture;
    frame_pacer_gl_bind_sampler_fn gl_bind_sampler;
    frame_pacer_gl_bind_framebuffer_fn gl_bind_framebuffer;
    frame_pacer_gl_viewport_fn gl_viewport;
    frame_pacer_gl_scissor_fn gl_scissor;
    frame_pacer_gl_color_mask_fn gl_color_mask;
    frame_pacer_gl_create_shader_fn gl_create_shader;
    frame_pacer_gl_shader_source_fn gl_shader_source;
    frame_pacer_gl_compile_shader_fn gl_compile_shader;
    frame_pacer_gl_get_shader_iv_fn gl_get_shader_iv;
    frame_pacer_gl_get_shader_info_log_fn gl_get_shader_info_log;
    frame_pacer_gl_delete_shader_fn gl_delete_shader;
    frame_pacer_gl_create_program_fn gl_create_program;
    frame_pacer_gl_attach_shader_fn gl_attach_shader;
    frame_pacer_gl_bind_attrib_location_fn gl_bind_attrib_location;
    frame_pacer_gl_link_program_fn gl_link_program;
    frame_pacer_gl_get_program_iv_fn gl_get_program_iv;
    frame_pacer_gl_get_program_info_log_fn gl_get_program_info_log;
    frame_pacer_gl_delete_program_fn gl_delete_program;
    frame_pacer_gl_use_program_fn gl_use_program;
    frame_pacer_gl_get_uniform_location_fn gl_get_uniform_location;
    frame_pacer_gl_uniform_2f_fn gl_uniform_2f;
    frame_pacer_gl_gen_vertex_arrays_fn gl_gen_vertex_arrays;
    frame_pacer_gl_delete_vertex_arrays_fn gl_delete_vertex_arrays;
    frame_pacer_gl_bind_vertex_array_fn gl_bind_vertex_array;
    frame_pacer_gl_gen_buffers_fn gl_gen_buffers;
    frame_pacer_gl_delete_buffers_fn gl_delete_buffers;
    frame_pacer_gl_bind_buffer_fn gl_bind_buffer;
    frame_pacer_gl_buffer_data_fn gl_buffer_data;
    frame_pacer_gl_enable_vertex_attrib_array_fn gl_enable_vertex_attrib_array;
    frame_pacer_gl_vertex_attrib_pointer_fn gl_vertex_attrib_pointer;
    frame_pacer_gl_draw_arrays_fn gl_draw_arrays;
    bool hud_available;
};

#if defined(__GNUC__)
#define FRAME_PACER_GL_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_GL_INTERNAL
#endif

FRAME_PACER_GL_INTERNAL bool frame_pacer_gl_dispatch_init(
    struct frame_pacer_gl_dispatch *);
FRAME_PACER_GL_INTERNAL void frame_pacer_gl_dispatch_destroy(
    struct frame_pacer_gl_dispatch *);

#endif
