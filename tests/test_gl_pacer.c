#include <EGL/egl.h>
#include <GL/glx.h>
#include <assert.h>
#include <dlfcn.h>
#include <string.h>

int main(void)
{
    void *provider;
    void *symbol;
    __GLXextFuncPtr glx_proc;
    __GLXextFuncPtr (*glx_get_proc)(const GLubyte *);
    void (*glx_swap)(Display *, GLXDrawable);
    EGLBoolean (*egl_swap)(EGLDisplay, EGLSurface);
    EGLBoolean (*egl_swap_damage)(EGLDisplay, EGLSurface, const EGLint *, EGLint);
    unsigned int (*glx_calls)(void);
    unsigned int (*egl_calls)(void);
    void (*set_egl_context)(GLboolean);
    unsigned int (*gl_vertices)(void);
    unsigned int (*gl_state_preserved)(void);
    unsigned int (*native_vertex_shader)(void);

    symbol = dlsym(RTLD_DEFAULT, "glXSwapBuffers");
    assert(symbol);
    memcpy(&glx_swap, &symbol, sizeof(glx_swap));
    symbol = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    assert(symbol);
    memcpy(&egl_swap, &symbol, sizeof(egl_swap));
    symbol = dlsym(RTLD_DEFAULT, "glXGetProcAddress");
    assert(symbol);
    memcpy(&glx_get_proc, &symbol, sizeof(glx_get_proc));
    assert(dlsym(RTLD_DEFAULT, "setenv"));

    provider = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    assert(provider);
    glx_swap(0, 0);
    glx_swap(0, 0);
    symbol = dlsym(provider, "frame_pacer_test_set_egl_context");
    assert(symbol);
    memcpy(&set_egl_context, &symbol, sizeof(set_egl_context));
    set_egl_context(GL_TRUE);
    assert(egl_swap(EGL_NO_DISPLAY, EGL_NO_SURFACE) == EGL_TRUE);
    symbol = dlsym(RTLD_DEFAULT, "eglSwapBuffersWithDamageKHR");
    assert(symbol);
    memcpy(&egl_swap_damage, &symbol, sizeof(egl_swap_damage));
    assert(egl_swap_damage((EGLDisplay)1, (EGLSurface)1, 0, 0) == EGL_TRUE);
    symbol = dlsym(provider, "glXSwapBuffers");
    assert(symbol);
    memcpy(&glx_swap, &symbol, sizeof(glx_swap));
    glx_swap(0, 0);
    glx_proc = glx_get_proc((const GLubyte *)"eglSwapBuffers");
    _Static_assert(sizeof(symbol) == sizeof(glx_proc), "unsupported function pointer representation");
    memcpy(&symbol, &glx_proc, sizeof(symbol));
    assert(symbol);
    memcpy(&egl_swap, &symbol, sizeof(egl_swap));
    assert(egl_swap(EGL_NO_DISPLAY, EGL_NO_SURFACE) == EGL_TRUE);
    symbol = dlsym(provider, "frame_pacer_test_glx_calls");
    assert(symbol);
    memcpy(&glx_calls, &symbol, sizeof(glx_calls));
    symbol = dlsym(provider, "frame_pacer_test_egl_calls");
    assert(symbol);
    memcpy(&egl_calls, &symbol, sizeof(egl_calls));
    assert(glx_calls() == 3);
    assert(egl_calls() == 3);
    symbol = dlsym(provider, "frame_pacer_test_gl_vertices");
    assert(symbol);
    memcpy(&gl_vertices, &symbol, sizeof(gl_vertices));
    assert(gl_vertices() > 12);
    symbol = dlsym(provider, "frame_pacer_test_native_vertex_shader");
    assert(symbol);
    memcpy(&native_vertex_shader, &symbol, sizeof(native_vertex_shader));
    assert(native_vertex_shader());
    symbol = dlsym(provider, "frame_pacer_test_gl_state_preserved");
    assert(symbol);
    memcpy(&gl_state_preserved, &symbol, sizeof(gl_state_preserved));
    assert(gl_state_preserved() == 0x7ffU);
    assert(dlclose(provider) == 0);
    return 0;
}
