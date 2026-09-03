#define _GNU_SOURCE
#include "gl_hud_renderer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GL_ARRAY_BUFFER_ 0x8892U
#define GL_ARRAY_BUFFER_BINDING_ 0x8894U
#define GL_STATIC_DRAW_ 0x88E4U
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9U
#define GL_DRAW_FRAMEBUFFER_BINDING_ 0x8CA6U
#define GL_ACTIVE_TEXTURE_ 0x84E0U
#define GL_TEXTURE_BINDING_2D_ 0x8069U
#define GL_TEXTURE0_ 0x84C0U
#define GL_SAMPLER_BINDING_ 0x8919U
#define GL_SCISSOR_BOX_ 0x0C10U
#define GL_BLEND_SRC_RGB_ 0x80C9U
#define GL_BLEND_DST_RGB_ 0x80C8U
#define GL_BLEND_SRC_ALPHA_ 0x80CBU
#define GL_BLEND_DST_ALPHA_ 0x80CAU
#define GL_BLEND_EQUATION_RGB_ 0x8009U
#define GL_BLEND_EQUATION_ALPHA_ 0x883DU
#define GL_FUNC_ADD_ 0x8006U
#define GL_FRAMEBUFFER_SRGB_ 0x8DB9U
#define GL_COLOR_WRITEMASK_ 0x0C23U
#define GL_VERTEX_ARRAY_BINDING_ 0x85B5U
#define GL_CURRENT_PROGRAM_ 0x8B8DU
#define GL_VERTEX_SHADER_ 0x8B31U
#define GL_FRAGMENT_SHADER_ 0x8B30U
#define GL_COMPILE_STATUS_ 0x8B81U
#define GL_LINK_STATUS_ 0x8B82U

struct frame_pacer_gl_hud_resource {
    void *context;
    void *display;
    enum frame_pacer_gl_context_api api;
    GLuint program, vao, vbo;
    GLint viewport_uniform;
    bool available;
    struct frame_pacer_gl_hud_resource *next;
};

struct gl_state {
    GLint framebuffer, active_texture, texture, sampler, program, vao;
    GLint array_buffer, viewport[4], scissor_box[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_equation_rgb, blend_equation_alpha, color_mask[4];
    GLboolean blend, cull, depth, stencil, scissor, srgb;
};

static void log_message(struct frame_pacer_gl_hud_renderer *renderer,
                        const char *format, ...)
{
    char message[512];
    va_list arguments;

    if (!renderer || !renderer->log)
        return;
    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    renderer->log(message);
}

static bool process_uses_wined3d(void)
{
    static const char needle[] = "wined3d.dll";
    char buffer[4096 + sizeof(needle)];
    size_t retained = 0;
    int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    ssize_t read_bytes;

    if (maps < 0)
        return false;
    // Context setup holds the renderer lock across shared vertices/resources.
    // Unlocking only this read lets concurrent rendering replace the vertices.
    // Detection runs on resource creation, never on an ordinary cached draw.
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    while ((read_bytes = read(maps, buffer + retained, 4096)) != 0) {
        size_t bytes;

        if (read_bytes < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        bytes = retained + (size_t)read_bytes;
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

static bool compile_shader(struct frame_pacer_gl_hud_renderer *renderer,
                           const struct frame_pacer_gl_dispatch *gl,
                           GLenum type, const char *source, GLuint *shader)
{
    GLint ok = 0;
    GLchar info[256] = {0};

    *shader = gl->gl_create_shader(type);
    if (!*shader)
        return false;
    gl->gl_shader_source(*shader, 1, (const GLchar *const *)&source, 0);
    gl->gl_compile_shader(*shader);
    gl->gl_get_shader_iv(*shader, GL_COMPILE_STATUS_, &ok);
    if (ok)
        return true;
    gl->gl_get_shader_info_log(*shader, (GLsizei)sizeof(info) - 1, 0, info);
    log_message(renderer,
                "frame-pacer: GL shader compile failed type=0x%x info=%s\n",
                type, info);
    gl->gl_delete_shader(*shader);
    *shader = 0;
    return false;
}

static struct frame_pacer_gl_hud_resource *
find_resource(struct frame_pacer_gl_hud_renderer *renderer,
              enum frame_pacer_gl_context_api api, void *display, void *context)
{
    struct frame_pacer_gl_hud_resource *resource;

    for (resource = renderer->resources; resource; resource = resource->next)
        if (resource->api == api && resource->display == display &&
            resource->context == context)
            return resource;
    return 0;
}

static bool create_resources(struct frame_pacer_gl_hud_renderer *renderer,
                             const struct frame_pacer_gl_dispatch *gl,
                             enum frame_pacer_gl_context_api api, void *display,
                             void *context,
                             struct frame_pacer_gl_hud_resource **out)
{
    static const char native_vertex[] =
        "#version 130\n"
        "in vec2 position; in vec4 color; out vec4 pass_color; uniform vec2 "
        "viewport;\n"
        "void main(){ gl_Position=vec4(position.x/viewport.x*2.0-1.0,"
        "position.y/viewport.y*-2.0+1.0,0.0,1.0); pass_color=color; }\n";
    static const char wined3d_vertex[] =
        "#version 130\n"
        "in vec2 position; in vec4 color; out vec4 pass_color; uniform vec2 "
        "viewport;\n"
        "void main(){ gl_Position=vec4(position.x/viewport.x*2.0-1.0,"
        "position.y/viewport.y*2.0-1.0,0.0,1.0); pass_color=color; }\n";
    static const char fragment[] = "#version 130\n"
                                   "in vec4 pass_color; out vec4 output_color; "
                                   "void main(){ output_color=pass_color; }\n";
    struct frame_pacer_gl_hud_resource *resource;
    GLuint vertex = 0, fragment_shader = 0;
    GLint ok = 0;
    GLchar info[256] = {0};
    const GLubyte *version;
    bool wined3d;

    if (!out)
        return false;
    *out = 0;
    resource = find_resource(renderer, api, display, context);
    if (resource) {
        *out = resource;
        return resource->available;
    }
    resource = calloc(1, sizeof(*resource));
    if (!resource)
        return false;
    resource->api = api;
    resource->display = display;
    resource->context = context;
    version = gl->gl_get_string(GL_VERSION);
    log_message(renderer, "frame-pacer: GL HUD context version=%s\n",
                version ? (const char *)version : "unavailable");
    wined3d = process_uses_wined3d();
    log_message(renderer, "frame-pacer: GL HUD coordinates=%s\n",
                wined3d ? "wined3d" : "native");
    if (!compile_shader(renderer, gl, GL_VERTEX_SHADER_,
                        wined3d ? wined3d_vertex : native_vertex, &vertex) ||
        !compile_shader(renderer, gl, GL_FRAGMENT_SHADER_, fragment,
                        &fragment_shader))
        goto fail;
    resource->program = gl->gl_create_program();
    if (!resource->program)
        goto fail;
    gl->gl_attach_shader(resource->program, vertex);
    gl->gl_attach_shader(resource->program, fragment_shader);
    gl->gl_bind_attrib_location(resource->program, 0, "position");
    gl->gl_bind_attrib_location(resource->program, 1, "color");
    gl->gl_link_program(resource->program);
    gl->gl_get_program_iv(resource->program, GL_LINK_STATUS_, &ok);
    gl->gl_delete_shader(vertex);
    gl->gl_delete_shader(fragment_shader);
    vertex = fragment_shader = 0;
    if (!ok) {
        gl->gl_get_program_info_log(resource->program,
                                    (GLsizei)sizeof(info) - 1, 0, info);
        log_message(renderer, "frame-pacer: GL program link failed info=%s\n",
                    info);
        goto fail;
    }
    resource->viewport_uniform =
        gl->gl_get_uniform_location(resource->program, "viewport");
    if (resource->viewport_uniform < 0)
        goto fail;
    gl->gl_gen_vertex_arrays(1, &resource->vao);
    gl->gl_gen_buffers(1, &resource->vbo);
    if (!resource->vao || !resource->vbo)
        goto fail;
    gl->gl_bind_vertex_array(resource->vao);
    gl->gl_bind_buffer(GL_ARRAY_BUFFER_, resource->vbo);
    gl->gl_enable_vertex_attrib_array(0);
    gl->gl_vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE,
                                 (GLsizei)sizeof(struct frame_pacer_hud_vertex),
                                 0);
    gl->gl_enable_vertex_attrib_array(1);
    gl->gl_vertex_attrib_pointer(1, 4, GL_FLOAT, GL_FALSE,
                                 (GLsizei)sizeof(struct frame_pacer_hud_vertex),
                                 (const void *)(2 * sizeof(float)));
    resource->next = renderer->resources;
    renderer->resources = resource;
    resource->available = true;
    *out = resource;
    return true;
fail:
    log_message(renderer, "frame-pacer: GL HUD resources unavailable\n");
    if (vertex)
        gl->gl_delete_shader(vertex);
    if (fragment_shader)
        gl->gl_delete_shader(fragment_shader);
    if (resource->vbo)
        gl->gl_delete_buffers(1, &resource->vbo);
    if (resource->vao)
        gl->gl_delete_vertex_arrays(1, &resource->vao);
    if (resource->program)
        gl->gl_delete_program(resource->program);
    resource->program = resource->vao = resource->vbo = 0;
    resource->next = renderer->resources;
    renderer->resources = resource;
    *out = resource;
    return false;
}

static void save_state(const struct frame_pacer_gl_dispatch *gl,
                       struct gl_state *state)
{
    gl->gl_get_integer(GL_DRAW_FRAMEBUFFER_BINDING_, &state->framebuffer);
    gl->gl_get_integer(GL_ACTIVE_TEXTURE_, &state->active_texture);
    gl->gl_active_texture(GL_TEXTURE0_);
    gl->gl_get_integer(GL_TEXTURE_BINDING_2D_, &state->texture);
    gl->gl_get_integer(GL_SAMPLER_BINDING_, &state->sampler);
    gl->gl_get_integer(GL_CURRENT_PROGRAM_, &state->program);
    gl->gl_get_integer(GL_VERTEX_ARRAY_BINDING_, &state->vao);
    gl->gl_get_integer(GL_ARRAY_BUFFER_BINDING_, &state->array_buffer);
    gl->gl_get_integer(GL_VIEWPORT, state->viewport);
    gl->gl_get_integer(GL_SCISSOR_BOX_, state->scissor_box);
    gl->gl_get_integer(GL_COLOR_WRITEMASK_, state->color_mask);
    gl->gl_get_integer(GL_BLEND_SRC_RGB_, &state->blend_src_rgb);
    gl->gl_get_integer(GL_BLEND_DST_RGB_, &state->blend_dst_rgb);
    gl->gl_get_integer(GL_BLEND_SRC_ALPHA_, &state->blend_src_alpha);
    gl->gl_get_integer(GL_BLEND_DST_ALPHA_, &state->blend_dst_alpha);
    gl->gl_get_integer(GL_BLEND_EQUATION_RGB_, &state->blend_equation_rgb);
    gl->gl_get_integer(GL_BLEND_EQUATION_ALPHA_, &state->blend_equation_alpha);
    state->blend = gl->gl_is_enabled(GL_BLEND);
    state->cull = gl->gl_is_enabled(GL_CULL_FACE);
    state->depth = gl->gl_is_enabled(GL_DEPTH_TEST);
    state->stencil = gl->gl_is_enabled(GL_STENCIL_TEST);
    state->scissor = gl->gl_is_enabled(GL_SCISSOR_TEST);
    state->srgb = gl->gl_is_enabled(GL_FRAMEBUFFER_SRGB_);
}

static void restore_state(const struct frame_pacer_gl_dispatch *gl,
                          const struct gl_state *state)
{
    gl->gl_use_program((GLuint)state->program);
    gl->gl_active_texture(GL_TEXTURE0_);
    gl->gl_bind_texture(GL_TEXTURE_2D, (GLuint)state->texture);
    gl->gl_bind_sampler(0, (GLuint)state->sampler);
    gl->gl_active_texture((GLenum)state->active_texture);
    gl->gl_bind_vertex_array((GLuint)state->vao);
    gl->gl_bind_buffer(GL_ARRAY_BUFFER_, (GLuint)state->array_buffer);
    gl->gl_blend_equation_separate((GLenum)state->blend_equation_rgb,
                                   (GLenum)state->blend_equation_alpha);
    gl->gl_blend_func_separate(
        (GLenum)state->blend_src_rgb, (GLenum)state->blend_dst_rgb,
        (GLenum)state->blend_src_alpha, (GLenum)state->blend_dst_alpha);
#define RESTORE_CAP(field_, cap_)                                              \
    do {                                                                       \
        if (state->field_)                                                     \
            gl->gl_enable(cap_);                                               \
        else                                                                   \
            gl->gl_disable(cap_);                                              \
    } while (0)
    RESTORE_CAP(blend, GL_BLEND);
    RESTORE_CAP(cull, GL_CULL_FACE);
    RESTORE_CAP(depth, GL_DEPTH_TEST);
    RESTORE_CAP(stencil, GL_STENCIL_TEST);
    RESTORE_CAP(scissor, GL_SCISSOR_TEST);
    RESTORE_CAP(srgb, GL_FRAMEBUFFER_SRGB_);
#undef RESTORE_CAP
    gl->gl_viewport(state->viewport[0], state->viewport[1], state->viewport[2],
                    state->viewport[3]);
    gl->gl_scissor(state->scissor_box[0], state->scissor_box[1],
                   state->scissor_box[2], state->scissor_box[3]);
    gl->gl_color_mask(
        (GLboolean)state->color_mask[0], (GLboolean)state->color_mask[1],
        (GLboolean)state->color_mask[2], (GLboolean)state->color_mask[3]);
    gl->gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_, (GLuint)state->framebuffer);
}

void frame_pacer_gl_hud_render(struct frame_pacer_gl_hud_renderer *renderer,
                               const struct frame_pacer_gl_dispatch *gl,
                               const struct frame_pacer_hud_text *text,
                               EGLDisplay egl_display, EGLSurface egl_surface,
                               Display *glx_display, GLXDrawable glx_drawable)
{
    struct frame_pacer_gl_hud_resource *resource;
    struct gl_state state;
    enum frame_pacer_gl_context_api api;
    void *display, *context;
    GLint viewport[4];
    unsigned int width = 0, height = 0;

    if (!renderer || !renderer->vertices || !gl || !text)
        return;
    (void)pthread_mutex_lock(&renderer->mutex);
    if (glx_display) {
        api = FRAME_PACER_GL_CONTEXT_GLX;
        display = glx_display;
        context = gl->next_glx_current_context
                      ? (void *)gl->next_glx_current_context()
                      : 0;
    } else {
        api = FRAME_PACER_GL_CONTEXT_EGL;
        display = egl_display;
        context = gl->next_egl_current_context
                      ? (void *)gl->next_egl_current_context()
                      : 0;
    }
    if (!context)
        goto out;
    gl->gl_get_integer(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        goto out;
    if (glx_display && gl->next_glx_query_drawable) {
        gl->next_glx_query_drawable(glx_display, glx_drawable, GLX_WIDTH,
                                    &width);
        gl->next_glx_query_drawable(glx_display, glx_drawable, GLX_HEIGHT,
                                    &height);
    } else if (egl_display != EGL_NO_DISPLAY && egl_surface != EGL_NO_SURFACE &&
               gl->next_egl_query_surface) {
        EGLint egl_width = 0, egl_height = 0;

        if (gl->next_egl_query_surface(egl_display, egl_surface, EGL_WIDTH,
                                       &egl_width) &&
            egl_width > 0)
            width = (unsigned int)egl_width;
        if (gl->next_egl_query_surface(egl_display, egl_surface, EGL_HEIGHT,
                                       &egl_height) &&
            egl_height > 0)
            height = (unsigned int)egl_height;
    }
    if (!width || !height) {
        width = (unsigned int)viewport[2];
        height = (unsigned int)viewport[3];
    }
    save_state(gl, &state);
    if (!frame_pacer_hud_vertices_build_for_extent(renderer->vertices, text,
                                                   width, height) ||
        !create_resources(renderer, gl, api, display, context, &resource)) {
        restore_state(gl, &state);
        goto out;
    }
    gl->gl_bind_framebuffer(GL_DRAW_FRAMEBUFFER_, 0);
    gl->gl_active_texture(GL_TEXTURE0_);
    gl->gl_bind_sampler(0, 0);
    gl->gl_viewport(0, 0, (GLsizei)width, (GLsizei)height);
    gl->gl_scissor(0, 0, (GLsizei)width, (GLsizei)height);
    gl->gl_disable(GL_DEPTH_TEST);
    gl->gl_disable(GL_CULL_FACE);
    gl->gl_disable(GL_STENCIL_TEST);
    gl->gl_disable(GL_FRAMEBUFFER_SRGB_);
    gl->gl_enable(GL_BLEND);
    gl->gl_blend_equation_separate(GL_FUNC_ADD_, GL_FUNC_ADD_);
    gl->gl_blend_func_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                               GL_ONE_MINUS_SRC_ALPHA);
    gl->gl_enable(GL_SCISSOR_TEST);
    gl->gl_color_mask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->gl_use_program(resource->program);
    gl->gl_uniform_2f(resource->viewport_uniform, (GLfloat)width,
                      (GLfloat)height);
    gl->gl_bind_vertex_array(resource->vao);
    gl->gl_bind_buffer(GL_ARRAY_BUFFER_, resource->vbo);
    gl->gl_buffer_data(GL_ARRAY_BUFFER_,
                       (GLsizeiptr)renderer->vertices->count *
                           (GLsizeiptr)sizeof(renderer->vertices->data[0]),
                       renderer->vertices->data, GL_STATIC_DRAW_);
    gl->gl_draw_arrays(GL_TRIANGLES, 0, (GLsizei)renderer->vertices->count);
    restore_state(gl, &state);
out:
    (void)pthread_mutex_unlock(&renderer->mutex);
}

void frame_pacer_gl_hud_forget(struct frame_pacer_gl_hud_renderer *renderer,
                               enum frame_pacer_gl_context_api api,
                               void *display, void *context,
                               bool all_display_contexts)
{
    struct frame_pacer_gl_hud_resource **link;

    if (!renderer)
        return;
    (void)pthread_mutex_lock(&renderer->mutex);
    link = &renderer->resources;
    while (*link) {
        struct frame_pacer_gl_hud_resource *resource = *link;
        bool matches = resource->api == api && resource->display == display &&
                       (all_display_contexts || resource->context == context);

        if (!matches) {
            link = &resource->next;
            continue;
        }
        *link = resource->next;
        /* Its context provider owns the GL objects once that context dies. */
        free(resource);
    }
    (void)pthread_mutex_unlock(&renderer->mutex);
}

void frame_pacer_gl_hud_renderer_destroy(
    struct frame_pacer_gl_hud_renderer *renderer)
{
    struct frame_pacer_gl_hud_resource *resource;

    if (!renderer)
        return;
    (void)pthread_mutex_lock(&renderer->mutex);
    resource = renderer->resources;
    renderer->resources = 0;
    while (resource) {
        struct frame_pacer_gl_hud_resource *next = resource->next;

        free(resource);
        resource = next;
    }
    (void)pthread_mutex_unlock(&renderer->mutex);
}
