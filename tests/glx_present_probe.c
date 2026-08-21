#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include "hud_vertices.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
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
        };

        window = XCreateWindow(display, RootWindow(display, visual->screen),
                               0, 0, FRAME_PACER_HUD_WIDTH_MAX + 32U,
                               FRAME_PACER_HUD_HEIGHT_MAX + 32U, 0,
                               visual->depth, InputOutput,
                               visual->visual, CWColormap | CWEventMask,
                               &window_attributes);
    }
    if (!window) goto cleanup;
    XMapWindow(display, window);
    XSync(display, False);
    context = glXCreateContext(display, visual, 0, True);
    if (!context || !glXMakeCurrent(display, window, context)) {
        fputs("GLX context unavailable\n", stderr);
        goto cleanup;
    }
    glXQueryDrawable(display, window, GLX_WIDTH, &drawable_width);
    glXQueryDrawable(display, window, GLX_HEIGHT, &drawable_height);
    if (drawable_width < FRAME_PACER_HUD_WIDTH_MAX ||
        drawable_height < FRAME_PACER_HUD_HEIGHT_MAX) {
        fputs("GLX drawable cannot contain the complete HUD\n", stderr);
        goto cleanup;
    }

    glViewport(3, 4, 80, 81);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(display, window);
    glXSwapBuffers(display, window);

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
    if (display && window) XDestroyWindow(display, window);
    if (display && colormap) XFreeColormap(display, colormap);
    if (visual) XFree(visual);
    if (display) XCloseDisplay(display);
    return exit_code;
}
