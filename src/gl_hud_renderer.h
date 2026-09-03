#ifndef FRAME_PACER_GL_HUD_RENDERER_H
#define FRAME_PACER_GL_HUD_RENDERER_H

#include "gl_pacer_dispatch.h"
#include "hud_text.h"
#include "hud_vertices.h"

#include <EGL/egl.h>
#include <GL/glx.h>
#include <pthread.h>
#include <stdbool.h>

enum frame_pacer_gl_context_api {
    FRAME_PACER_GL_CONTEXT_GLX,
    FRAME_PACER_GL_CONTEXT_EGL
};

struct frame_pacer_gl_hud_resource;

struct frame_pacer_gl_hud_renderer {
    pthread_mutex_t mutex;
    struct frame_pacer_gl_hud_resource *resources;
    struct frame_pacer_hud_vertices *vertices;
    void (*log)(const char *);
};

#define FRAME_PACER_GL_HUD_RENDERER_INITIALIZER(vertices_, log_)               \
    {.mutex = PTHREAD_MUTEX_INITIALIZER, .vertices = (vertices_), .log = (log_)}

#if defined(__GNUC__)
#define FRAME_PACER_GL_HUD_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_GL_HUD_INTERNAL
#endif

FRAME_PACER_GL_HUD_INTERNAL void
frame_pacer_gl_hud_render(struct frame_pacer_gl_hud_renderer *,
                          const struct frame_pacer_gl_dispatch *,
                          const struct frame_pacer_hud_text *, EGLDisplay,
                          EGLSurface, Display *, GLXDrawable);
FRAME_PACER_GL_HUD_INTERNAL void
frame_pacer_gl_hud_forget(struct frame_pacer_gl_hud_renderer *,
                          enum frame_pacer_gl_context_api, void *, void *,
                          bool);
FRAME_PACER_GL_HUD_INTERNAL void
frame_pacer_gl_hud_renderer_destroy(struct frame_pacer_gl_hud_renderer *);

#endif
