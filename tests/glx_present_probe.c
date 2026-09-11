#define _POSIX_C_SOURCE 200809L
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include "present_extent.h"
#include "present_benchmark.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    struct present_benchmark benchmark;
    benchmark_init(&benchmark, PRESENT_CONTENT_WIDTH + 32U,
                   PRESENT_CONTENT_HEIGHT + 32U);
    int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
    Display *display = XOpenDisplay(0);
    XVisualInfo *visual = 0;
    Colormap colormap = 0;
    Window window = 0;
    GLXContext context = 0;
    int exit_code = 1;
    GLint viewport[4];
    unsigned int drawable_width = 0;
    unsigned int drawable_height = 0;
    GLuint unpack_buffer = 0, application_texture = 0;

    if (!display) {
        fputs("X11 display unavailable\n", stderr);
        return 77;
    }
    visual = glXChooseVisual(display, DefaultScreen(display), attributes);
    if (!visual) {
        fputs("GLX visual unavailable\n", stderr);
        goto cleanup;
    }
    colormap = XCreateColormap(display, RootWindow(display, visual->screen),
                               visual->visual, AllocNone);
    {
        XSetWindowAttributes window_attributes = {
            .colormap = colormap,
            .event_mask = StructureNotifyMask,
            .override_redirect = True,
        };

        window = XCreateWindow(
            display, RootWindow(display, visual->screen), 0, 0, benchmark.width,
            benchmark.height, 0, visual->depth, InputOutput, visual->visual,
            CWColormap | CWEventMask | CWOverrideRedirect, &window_attributes);
    }
    if (!window)
        goto cleanup;
    XMapWindow(display, window);
    XSync(display, False);
    context = glXCreateContext(display, visual, 0, True);
    if (!context || !glXMakeCurrent(display, window, context)) {
        fputs("GLX context unavailable\n", stderr);
        goto cleanup;
    }
    if (getenv("FRAME_PACER_BENCH_FRAMES")) {
        typedef void (*swap_interval_fn)(Display *, GLXDrawable, int);
        swap_interval_fn swap_interval = (swap_interval_fn)glXGetProcAddressARB(
            (const GLubyte *)"glXSwapIntervalEXT");
        const char *extensions =
            glXQueryExtensionsString(display, visual->screen);
        if (!swap_interval || !extensions ||
            !strstr(extensions, "GLX_EXT_swap_control")) {
            fputs("Benchmark requires GLX_EXT_swap_control\n", stderr);
            goto cleanup;
        }
        swap_interval(display, window, 0);
        fprintf(stderr, "gl_vendor=%s\ngl_renderer=%s\ngl_version=%s\n",
                glGetString(GL_VENDOR), glGetString(GL_RENDERER),
                glGetString(GL_VERSION));
    }
    glXQueryDrawable(display, window, GLX_WIDTH, &drawable_width);
    glXQueryDrawable(display, window, GLX_HEIGHT, &drawable_height);
    if (drawable_width != benchmark.width ||
        drawable_height != benchmark.height) {
        fputs("GLX probe requires the exact requested extent\n", stderr);
        goto cleanup;
    }
    if (drawable_width < PRESENT_CONTENT_WIDTH ||
        drawable_height < PRESENT_CONTENT_HEIGHT) {
        fputs("GLX drawable cannot contain the complete HUD\n", stderr);
        goto cleanup;
    }

    glViewport(3, 4, 80, 81);
    glGenBuffers(1, &unpack_buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &application_texture);
    glBindTexture(GL_TEXTURE_2D, application_texture);
    glActiveTexture(GL_TEXTURE3);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (unsigned int frame = 0; frame < benchmark.frames; ++frame) {
        benchmark_begin(&benchmark);
        if (benchmark_resize_due(&benchmark)) {
            benchmark_resize(&benchmark);
            XResizeWindow(display, window, benchmark.width, benchmark.height);
            XSync(display, False);
            glXWaitX();
            glXQueryDrawable(display, window, GLX_WIDTH, &drawable_width);
            glXQueryDrawable(display, window, GLX_HEIGHT, &drawable_height);
            if (drawable_width != benchmark.width ||
                drawable_height != benchmark.height)
                goto cleanup;
        }
        glClear(GL_COLOR_BUFFER_BIT);
        glXSwapBuffers(display, window);
        glFinish();
        benchmark_end(&benchmark);
    }
    benchmark_report(&benchmark);

    {
        const GLenum names[] = {GL_PIXEL_UNPACK_BUFFER_BINDING,
                                GL_UNPACK_ALIGNMENT,
                                GL_UNPACK_ROW_LENGTH,
                                GL_UNPACK_SKIP_PIXELS,
                                GL_UNPACK_SKIP_ROWS,
                                GL_ACTIVE_TEXTURE};
        const GLint expected[] = {(GLint)unpack_buffer, 8, 17, 3, 2,
                                  GL_TEXTURE3};
        for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            GLint actual = 0;
            glGetIntegerv(names[i], &actual);
            if (actual != expected[i]) {
                fprintf(stderr, "GL upload state not restored: 0x%x\n",
                        names[i]);
                goto cleanup;
            }
        }
        GLint texture = 0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        if (texture != (GLint)application_texture ||
            glGetError() != GL_NO_ERROR) {
            fputs("GL texture state or upload failed\n", stderr);
            goto cleanup;
        }
        glDeleteTextures(1, &application_texture);
        glDeleteBuffers(1, &unpack_buffer);
    }

    glGetIntegerv(GL_VIEWPORT, viewport);
    if (memcmp(viewport, (GLint[4]){3, 4, 80, 81}, sizeof(viewport)) ||
        glIsEnabled(GL_BLEND) || !glIsEnabled(GL_DEPTH_TEST)) {
        fputs("GL state was not restored\n", stderr);
        goto cleanup;
    }
    exit_code = 0;

cleanup:
    if (display && context) {
        (void)glXMakeCurrent(display, None, 0);
        glXDestroyContext(display, context);
    }
    if (display && window)
        XDestroyWindow(display, window);
    if (display && colormap)
        XFreeColormap(display, colormap);
    if (visual)
        XFree(visual);
    if (display)
        XCloseDisplay(display);
    return exit_code;
}
