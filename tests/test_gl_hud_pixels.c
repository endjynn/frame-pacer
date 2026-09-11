#define GL_GLEXT_PROTOTYPES
#include "gl_hud_renderer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_message(const char *message)
{
    fputs(message, stderr);
}

static void check_pixels(struct frame_pacer_gl_hud_renderer *renderer,
                         const struct frame_pacer_gl_dispatch *dispatch,
                         Display *display, Window window, unsigned int width,
                         unsigned int height, unsigned int rows)
{
    struct frame_pacer_hud_text text = {{"GPU 100%  61\x7f", "CPU  83%  73\x7f",
                                         "THR  50%  50%",
                                         "FPS 999\x7e 999\x7e"},
                                        4};
    if (rows == 3) {
        memcpy(text.lines[2], text.lines[3], sizeof(text.lines[2]));
        text.line_count = 3;
    }
    struct frame_pacer_hud_vertices reference;
    assert(frame_pacer_hud_vertices_build_for_extent(&reference, &text, width,
                                                     height));
    unsigned int panel_width = (unsigned int)reference.data[1].position[0];
    unsigned int panel_height = (unsigned int)reference.data[2].position[1];
    size_t bytes = (size_t)panel_width * panel_height * 4;
    unsigned char *expected = malloc(bytes), *actual = malloc(bytes);
    assert(expected && actual);
    XResizeWindow(display, window, width, height);
    XSync(display, False);
    glXWaitX();
    glXSwapBuffers(display, window);
    unsigned int drawable_width = 0, drawable_height = 0;
    glXQueryDrawable(display, window, GLX_WIDTH, &drawable_width);
    glXQueryDrawable(display, window, GLX_HEIGHT, &drawable_height);
    assert(drawable_width == width && drawable_height == height);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glDisable(GL_DITHER);
    glClearColor(0.25f, 0.375f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, (GLint)(height - panel_height), (GLsizei)panel_width,
                 (GLsizei)panel_height, GL_RGBA, GL_UNSIGNED_BYTE, expected);
    frame_pacer_gl_hud_render(renderer, dispatch, &text, EGL_NO_DISPLAY,
                              EGL_NO_SURFACE, display, window);
    glReadPixels(0, (GLint)(height - panel_height), (GLsizei)panel_width,
                 (GLsizei)panel_height, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    assert(glGetError() == GL_NO_ERROR);
    const struct frame_pacer_font_atlas *atlas = reference.atlas;
    const unsigned char *coverage = frame_pacer_font_atlas_pixels(atlas);
    for (unsigned int i = 0; i < reference.count; i += 6) {
        const struct frame_pacer_hud_vertex *quad = reference.data + i;
        unsigned int x0 = (unsigned int)quad[0].position[0];
        unsigned int y0 = (unsigned int)quad[0].position[1];
        unsigned int x1 = (unsigned int)quad[2].position[0];
        unsigned int y1 = (unsigned int)quad[2].position[1];
        for (unsigned int y = y0; y < y1; ++y) {
            for (unsigned int x = x0; x < x1; ++x) {
                float alpha = quad[0].color[3];
                if (i) {
                    unsigned int u =
                        (unsigned int)(quad[0].uv[0] * atlas->width + 0.5f) +
                        x - x0;
                    unsigned int v =
                        (unsigned int)(quad[0].uv[1] * atlas->height + 0.5f) +
                        y - y0;
                    alpha *= coverage[v * atlas->width + u] / 255.0f;
                }
                unsigned char *pixel =
                    expected +
                    ((size_t)(panel_height - 1 - y) * panel_width + x) * 4;
                for (unsigned int channel = 0; channel < 3; ++channel)
                    pixel[channel] =
                        (unsigned char)(quad[0].color[channel] * 255.0f *
                                            alpha +
                                        pixel[channel] * (1.0f - alpha) + 0.5f);
            }
        }
    }
    size_t mismatches = 0;
    for (size_t i = 0; i < bytes; ++i) {
        int delta = (int)expected[i] - actual[i];
        if (delta < -3 || delta > 3) {
            if (mismatches < 8)
                fprintf(stderr, "pixel byte %zu expected=%u actual=%u\n", i,
                        expected[i], actual[i]);
            ++mismatches;
        }
    }
    fprintf(stderr, "GL atlas pixels %ux%u rows=%u size=%u: %zu mismatches\n",
            width, height, rows, atlas->pixel_size, mismatches);
    assert(!mismatches);
    free(actual);
    free(expected);
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display)
        return 77;
    int attributes[] = {GLX_RGBA,
                        GLX_DOUBLEBUFFER,
                        GLX_RED_SIZE,
                        8,
                        GLX_GREEN_SIZE,
                        8,
                        GLX_BLUE_SIZE,
                        8,
                        None};
    XVisualInfo *visual =
        glXChooseVisual(display, DefaultScreen(display), attributes);
    assert(visual);
    Colormap colormap =
        XCreateColormap(display, RootWindow(display, visual->screen),
                        visual->visual, AllocNone);
    XSetWindowAttributes settings = {.colormap = colormap,
                                     .override_redirect = True};
    Window window =
        XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 320,
                      200, 0, visual->depth, InputOutput, visual->visual,
                      CWColormap | CWOverrideRedirect, &settings);
    assert(window);
    XMapWindow(display, window);
    XSync(display, False);
    GLXContext context = glXCreateContext(display, visual, NULL, True);
    assert(context && glXMakeCurrent(display, window, context));
    struct frame_pacer_gl_dispatch dispatch;
    assert(frame_pacer_gl_dispatch_init(&dispatch) && dispatch.hud_available);
    struct frame_pacer_hud_vertices vertices;
    struct frame_pacer_gl_hud_renderer renderer =
        FRAME_PACER_GL_HUD_RENDERER_INITIALIZER(&vertices, log_message);
    const unsigned int extents[][2] = {
        {1280, 720},  {1440, 900},  {1920, 1080}, {2560, 1440}, {2560, 1600},
        {3840, 2160}, {3440, 1440}, {87, 400},    {100, 100},   {1440, 900}};
    for (unsigned int i = 0; i < sizeof(extents) / sizeof(extents[0]); ++i)
        for (unsigned int rows = 3; rows <= 4; ++rows)
            check_pixels(&renderer, &dispatch, display, window, extents[i][0],
                         extents[i][1], rows);
    frame_pacer_gl_hud_forget(&renderer, FRAME_PACER_GL_CONTEXT_GLX, display,
                              context, false);
    frame_pacer_gl_hud_renderer_destroy(&renderer);
    assert(!pthread_mutex_destroy(&renderer.mutex));
    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, context);
    frame_pacer_gl_dispatch_destroy(&dispatch);
    XDestroyWindow(display, window);
    XFreeColormap(display, colormap);
    XFree(visual);
    XCloseDisplay(display);
    return 0;
}
