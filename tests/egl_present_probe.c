#include <EGL/egl.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "hud_vertices.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE,
    };
    Display *x_display = XOpenDisplay(0);
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = 0;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    XVisualInfo visual_template = {0};
    XVisualInfo *visual = 0;
    Colormap colormap = 0;
    Window window = 0;
    EGLint visual_id = 0;
    EGLint config_count = 0;
    int visual_count = 0;
    int exit_code = 1;
    GLint viewport[4];
    EGLint surface_width = 0;
    EGLint surface_height = 0;

    if (!x_display) {
        fputs("X11 display unavailable\n", stderr);
        return 77;
    }
    display = eglGetDisplay((EGLNativeDisplayType)x_display);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, 0, 0) ||
        !eglBindAPI(EGL_OPENGL_API) ||
        !eglChooseConfig(display, config_attributes, &config, 1, &config_count) ||
        config_count != 1 ||
        !eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual_id)) {
        fputs("EGL display/configuration unavailable\n", stderr);
        goto cleanup;
    }
    visual_template.visualid = (VisualID)visual_id;
    visual = XGetVisualInfo(x_display, VisualIDMask, &visual_template, &visual_count);
    if (!visual || visual_count < 1) {
        fputs("EGL X11 visual unavailable\n", stderr);
        goto cleanup;
    }
    colormap = XCreateColormap(x_display, RootWindow(x_display, visual->screen),
                               visual->visual, AllocNone);
    {
        XSetWindowAttributes attributes = {
            .colormap = colormap,
            .event_mask = StructureNotifyMask,
        };

        window = XCreateWindow(x_display, RootWindow(x_display, visual->screen),
                               0, 0, FRAME_PACER_HUD_REFERENCE_WIDTH + 32U,
                               FRAME_PACER_HUD_REFERENCE_HEIGHT + 32U, 0,
                               visual->depth, InputOutput,
                               visual->visual, CWColormap | CWEventMask,
                               &attributes);
    }
    if (!window) goto cleanup;
    XMapWindow(x_display, window);
    XSync(x_display, False);
    surface = eglCreateWindowSurface(display, config,
                                     (EGLNativeWindowType)window, 0);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, 0);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fputs("EGL surface/context unavailable\n", stderr);
        goto cleanup;
    }
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &surface_height) ||
        surface_width < (EGLint)FRAME_PACER_HUD_REFERENCE_WIDTH ||
        surface_height < (EGLint)FRAME_PACER_HUD_REFERENCE_HEIGHT) {
        fputs("EGL surface cannot contain the complete HUD\n", stderr);
        goto cleanup;
    }

    glViewport(3, 4, 80, 81);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(display, surface) || !eglSwapBuffers(display, surface)) {
        fputs("EGL swap failed\n", stderr);
        goto cleanup;
    }

    glGetIntegerv(GL_VIEWPORT, viewport);
    if (memcmp(viewport, (GLint[4]){3, 4, 80, 81}, sizeof(viewport)) ||
        glIsEnabled(GL_BLEND) || !glIsEnabled(GL_DEPTH_TEST)) {
        fputs("GL state was not restored\n", stderr);
        goto cleanup;
    }
    exit_code = 0;

cleanup:
    if (display != EGL_NO_DISPLAY) {
        (void)eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) (void)eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) (void)eglDestroySurface(display, surface);
        (void)eglTerminate(display);
    }
    if (x_display && window) XDestroyWindow(x_display, window);
    if (x_display && colormap) XFreeColormap(x_display, colormap);
    if (visual) XFree(visual);
    if (x_display) XCloseDisplay(x_display);
    return exit_code;
}
