#define _GNU_SOURCE
#include "gl_pacer_dispatch.h"

#include <dlfcn.h>
#include <string.h>

#if __SIZEOF_POINTER__ == 8
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.2.5"
#else
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.0"
#endif

static void copy_function(void *symbol, void *function, size_t size)
{
    memcpy(function, &symbol, size);
}

static void gl_function(struct frame_pacer_gl_dispatch *dispatch, void *library,
                        const char *name, void *function, size_t size)
{
    void *symbol =
        dispatch->real_dlsym ? dispatch->real_dlsym(library, name) : 0;

    if (!symbol && dispatch->next_glx_get_proc) {
        __GLXextFuncPtr extension =
            dispatch->next_glx_get_proc((const GLubyte *)name);

        _Static_assert(sizeof(symbol) == sizeof(extension),
                       "unsupported function pointer representation");
        memcpy(&symbol, &extension, sizeof(symbol));
    }
    if (!symbol && dispatch->next_egl_get_proc) {
        __eglMustCastToProperFunctionPointerType extension =
            dispatch->next_egl_get_proc(name);

        _Static_assert(sizeof(symbol) == sizeof(extension),
                       "unsupported function pointer representation");
        memcpy(&symbol, &extension, sizeof(symbol));
    }
    copy_function(symbol, function, size);
}

#define LOAD_GL(field, name)                                                   \
    gl_function(dispatch, library, (name), &dispatch->field,                   \
                sizeof(dispatch->field))

static bool load_hud_dispatch(struct frame_pacer_gl_dispatch *dispatch,
                              void *library)
{
    LOAD_GL(next_glx_current_context, "glXGetCurrentContext");
    LOAD_GL(next_glx_query_drawable, "glXQueryDrawable");
    LOAD_GL(next_egl_current_context, "eglGetCurrentContext");
    LOAD_GL(next_egl_query_surface, "eglQuerySurface");
    LOAD_GL(gl_get_string, "glGetString");
    LOAD_GL(gl_get_integer, "glGetIntegerv");
    LOAD_GL(gl_disable, "glDisable");
    LOAD_GL(gl_enable, "glEnable");
    LOAD_GL(gl_blend_func_separate, "glBlendFuncSeparate");
    LOAD_GL(gl_blend_equation_separate, "glBlendEquationSeparate");
    LOAD_GL(gl_is_enabled, "glIsEnabled");
    LOAD_GL(gl_active_texture, "glActiveTexture");
    LOAD_GL(gl_bind_texture, "glBindTexture");
    LOAD_GL(gl_bind_sampler, "glBindSampler");
    LOAD_GL(gl_bind_framebuffer, "glBindFramebuffer");
    LOAD_GL(gl_viewport, "glViewport");
    LOAD_GL(gl_scissor, "glScissor");
    LOAD_GL(gl_color_mask, "glColorMask");
    LOAD_GL(gl_create_shader, "glCreateShader");
    LOAD_GL(gl_shader_source, "glShaderSource");
    LOAD_GL(gl_compile_shader, "glCompileShader");
    LOAD_GL(gl_get_shader_iv, "glGetShaderiv");
    LOAD_GL(gl_get_shader_info_log, "glGetShaderInfoLog");
    LOAD_GL(gl_delete_shader, "glDeleteShader");
    LOAD_GL(gl_create_program, "glCreateProgram");
    LOAD_GL(gl_attach_shader, "glAttachShader");
    LOAD_GL(gl_bind_attrib_location, "glBindAttribLocation");
    LOAD_GL(gl_link_program, "glLinkProgram");
    LOAD_GL(gl_get_program_iv, "glGetProgramiv");
    LOAD_GL(gl_get_program_info_log, "glGetProgramInfoLog");
    LOAD_GL(gl_delete_program, "glDeleteProgram");
    LOAD_GL(gl_use_program, "glUseProgram");
    LOAD_GL(gl_get_uniform_location, "glGetUniformLocation");
    LOAD_GL(gl_uniform_2f, "glUniform2f");
    LOAD_GL(gl_gen_vertex_arrays, "glGenVertexArrays");
    LOAD_GL(gl_delete_vertex_arrays, "glDeleteVertexArrays");
    LOAD_GL(gl_bind_vertex_array, "glBindVertexArray");
    LOAD_GL(gl_gen_buffers, "glGenBuffers");
    LOAD_GL(gl_delete_buffers, "glDeleteBuffers");
    LOAD_GL(gl_bind_buffer, "glBindBuffer");
    LOAD_GL(gl_buffer_data, "glBufferData");
    LOAD_GL(gl_enable_vertex_attrib_array, "glEnableVertexAttribArray");
    LOAD_GL(gl_vertex_attrib_pointer, "glVertexAttribPointer");
    LOAD_GL(gl_draw_arrays, "glDrawArrays");
    return (dispatch->next_glx_current_context ||
            dispatch->next_egl_current_context) &&
           dispatch->gl_get_string && dispatch->gl_get_integer &&
           dispatch->gl_disable && dispatch->gl_enable &&
           dispatch->gl_blend_func_separate &&
           dispatch->gl_blend_equation_separate && dispatch->gl_is_enabled &&
           dispatch->gl_active_texture && dispatch->gl_bind_texture &&
           dispatch->gl_bind_sampler && dispatch->gl_bind_framebuffer &&
           dispatch->gl_viewport && dispatch->gl_scissor &&
           dispatch->gl_color_mask && dispatch->gl_create_shader &&
           dispatch->gl_shader_source && dispatch->gl_compile_shader &&
           dispatch->gl_get_shader_iv && dispatch->gl_get_shader_info_log &&
           dispatch->gl_delete_shader && dispatch->gl_create_program &&
           dispatch->gl_attach_shader && dispatch->gl_bind_attrib_location &&
           dispatch->gl_link_program && dispatch->gl_get_program_iv &&
           dispatch->gl_get_program_info_log && dispatch->gl_delete_program &&
           dispatch->gl_use_program && dispatch->gl_get_uniform_location &&
           dispatch->gl_uniform_2f && dispatch->gl_gen_vertex_arrays &&
           dispatch->gl_delete_vertex_arrays &&
           dispatch->gl_bind_vertex_array && dispatch->gl_gen_buffers &&
           dispatch->gl_delete_buffers && dispatch->gl_bind_buffer &&
           dispatch->gl_buffer_data &&
           dispatch->gl_enable_vertex_attrib_array &&
           dispatch->gl_vertex_attrib_pointer && dispatch->gl_draw_arrays;
}

#undef LOAD_GL

#define LOAD_FROM(handle, field, name)                                         \
    copy_function(dispatch->real_dlsym((handle), (name)), &dispatch->field,    \
                  sizeof(dispatch->field))

#define LOAD_NEXT_IF_MISSING(field, name)                                      \
    do {                                                                       \
        if (!dispatch->field)                                                  \
            LOAD_FROM(RTLD_NEXT, field, (name));                               \
    } while (0)

bool frame_pacer_gl_dispatch_init(struct frame_pacer_gl_dispatch *dispatch)
{
    void *symbol;

    if (!dispatch)
        return false;
    memset(dispatch, 0, sizeof(*dispatch));
    dispatch->libc_reference = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
    symbol = dispatch->libc_reference
                 ? dlvsym(dispatch->libc_reference, "dlsym",
                          FRAME_PACER_GLIBC_DLSYM_VERSION)
                 : 0;
    _Static_assert(sizeof(dispatch->real_dlsym) == sizeof(symbol),
                   "unsupported function pointer representation");
    memcpy(&dispatch->real_dlsym, &symbol, sizeof(dispatch->real_dlsym));
    if (!dispatch->real_dlsym)
        return false;

    dispatch->gl_reference = dlopen("libGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
    dispatch->egl_reference = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (dispatch->gl_reference) {
        LOAD_FROM(dispatch->gl_reference, next_glx_swap, "glXSwapBuffers");
        LOAD_FROM(dispatch->gl_reference, next_egl_swap, "eglSwapBuffers");
        LOAD_FROM(dispatch->gl_reference, next_egl_swap_damage_khr,
                  "eglSwapBuffersWithDamageKHR");
        LOAD_FROM(dispatch->gl_reference, next_egl_swap_damage_ext,
                  "eglSwapBuffersWithDamageEXT");
        LOAD_FROM(dispatch->gl_reference, next_glx_get_proc,
                  "glXGetProcAddress");
        LOAD_FROM(dispatch->gl_reference, next_glx_destroy_context,
                  "glXDestroyContext");
        LOAD_FROM(dispatch->gl_reference, next_egl_destroy_context,
                  "eglDestroyContext");
        LOAD_FROM(dispatch->gl_reference, next_egl_terminate, "eglTerminate");
        LOAD_FROM(dispatch->gl_reference, next_egl_get_proc,
                  "eglGetProcAddress");
    }
    if (dispatch->egl_reference) {
        if (!dispatch->next_egl_swap)
            LOAD_FROM(dispatch->egl_reference, next_egl_swap, "eglSwapBuffers");
        if (!dispatch->next_egl_swap_damage_khr)
            LOAD_FROM(dispatch->egl_reference, next_egl_swap_damage_khr,
                      "eglSwapBuffersWithDamageKHR");
        if (!dispatch->next_egl_swap_damage_ext)
            LOAD_FROM(dispatch->egl_reference, next_egl_swap_damage_ext,
                      "eglSwapBuffersWithDamageEXT");
        if (!dispatch->next_egl_get_proc)
            LOAD_FROM(dispatch->egl_reference, next_egl_get_proc,
                      "eglGetProcAddress");
        if (!dispatch->next_egl_destroy_context)
            LOAD_FROM(dispatch->egl_reference, next_egl_destroy_context,
                      "eglDestroyContext");
        if (!dispatch->next_egl_terminate)
            LOAD_FROM(dispatch->egl_reference, next_egl_terminate,
                      "eglTerminate");
    }
    LOAD_NEXT_IF_MISSING(next_glx_swap, "glXSwapBuffers");
    LOAD_NEXT_IF_MISSING(next_egl_swap, "eglSwapBuffers");
    LOAD_NEXT_IF_MISSING(next_egl_swap_damage_khr,
                         "eglSwapBuffersWithDamageKHR");
    LOAD_NEXT_IF_MISSING(next_egl_swap_damage_ext,
                         "eglSwapBuffersWithDamageEXT");
    LOAD_NEXT_IF_MISSING(next_glx_get_proc, "glXGetProcAddress");
    LOAD_NEXT_IF_MISSING(next_egl_get_proc, "eglGetProcAddress");
    LOAD_NEXT_IF_MISSING(next_glx_destroy_context, "glXDestroyContext");
    LOAD_NEXT_IF_MISSING(next_egl_destroy_context, "eglDestroyContext");
    LOAD_NEXT_IF_MISSING(next_egl_terminate, "eglTerminate");
    dispatch->hud_available = load_hud_dispatch(
        dispatch, dispatch->gl_reference ? dispatch->gl_reference
                                         : dispatch->egl_reference);
    return true;
}

#undef LOAD_FROM
#undef LOAD_NEXT_IF_MISSING

void frame_pacer_gl_dispatch_destroy(struct frame_pacer_gl_dispatch *dispatch)
{
    if (!dispatch)
        return;
    if (dispatch->egl_reference)
        (void)dlclose(dispatch->egl_reference);
    if (dispatch->gl_reference)
        (void)dlclose(dispatch->gl_reference);
    if (dispatch->libc_reference)
        (void)dlclose(dispatch->libc_reference);
    memset(dispatch, 0, sizeof(*dispatch));
}
