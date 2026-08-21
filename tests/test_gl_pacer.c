#include <EGL/egl.h>
#include <GL/glx.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    void *provider;
    void *symbol;
    __GLXextFuncPtr glx_proc;
    __GLXextFuncPtr (*glx_get_proc)(const GLubyte *);
    __GLXextFuncPtr (*glx_get_proc_arb)(const GLubyte *);
    __eglMustCastToProperFunctionPointerType (*egl_get_proc)(const char *);
    void (*glx_swap)(Display *, GLXDrawable);
    EGLBoolean (*egl_swap)(EGLDisplay, EGLSurface);
    EGLBoolean (*egl_swap_damage)(EGLDisplay, EGLSurface, const EGLint *, EGLint);
    unsigned int (*glx_calls)(void);
    unsigned int (*egl_calls)(void);
    void (*set_egl_context)(GLboolean);
    void (*set_context)(uintptr_t, GLboolean);
    void (*glx_destroy)(Display *, GLXContext);
    EGLBoolean (*egl_destroy)(EGLDisplay, EGLContext);
    EGLBoolean (*egl_terminate)(EGLDisplay);
    unsigned int (*gl_vertices)(void);
    unsigned int (*gl_programs)(void);
    unsigned int (*gl_state_preserved)(void);
    unsigned int (*native_vertex_shader)(void);
    unsigned int (*glx_destroy_calls)(void);
    unsigned int (*egl_destroy_calls)(void);
    unsigned int (*egl_terminate_calls)(void);

    symbol = dlsym(RTLD_DEFAULT, "glXSwapBuffers");
    assert(symbol);
    memcpy(&glx_swap, &symbol, sizeof(glx_swap));
    symbol = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    assert(symbol);
    memcpy(&egl_swap, &symbol, sizeof(egl_swap));
    symbol = dlsym(RTLD_DEFAULT, "glXGetProcAddress");
    assert(symbol);
    memcpy(&glx_get_proc, &symbol, sizeof(glx_get_proc));
    symbol = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    assert(symbol);
    memcpy(&glx_get_proc_arb, &symbol, sizeof(glx_get_proc_arb));
    symbol = dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    assert(symbol);
    memcpy(&egl_get_proc, &symbol, sizeof(egl_get_proc));
    assert(dlsym(RTLD_DEFAULT, "setenv"));

    provider = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    assert(provider);
    glx_swap((Display *)1, 0);
    glx_swap((Display *)1, 0);
    symbol = dlsym(provider, "frame_pacer_test_set_egl_context");
    assert(symbol);
    memcpy(&set_egl_context, &symbol, sizeof(set_egl_context));
    symbol = dlsym(provider, "frame_pacer_test_set_context");
    assert(symbol);
    memcpy(&set_context, &symbol, sizeof(set_context));
    set_egl_context(GL_TRUE);
    assert(egl_swap((EGLDisplay)1, (EGLSurface)1) == EGL_TRUE);
    symbol = dlsym(RTLD_DEFAULT, "eglSwapBuffersWithDamageKHR");
    assert(symbol);
    memcpy(&egl_swap_damage, &symbol, sizeof(egl_swap_damage));
    assert(egl_swap_damage((EGLDisplay)1, (EGLSurface)1, 0, 0) == EGL_TRUE);
    symbol = dlsym(RTLD_DEFAULT, "eglSwapBuffersWithDamageEXT");
    assert(symbol);
    memcpy(&egl_swap_damage, &symbol, sizeof(egl_swap_damage));
    assert(egl_swap_damage((EGLDisplay)1, (EGLSurface)1, 0, 0) == EGL_TRUE);
    symbol = dlsym(provider, "glXSwapBuffers");
    assert(symbol);
    memcpy(&glx_swap, &symbol, sizeof(glx_swap));
    glx_swap((Display *)1, 0);
    glx_proc = glx_get_proc((const GLubyte *)"eglSwapBuffers");
    _Static_assert(sizeof(symbol) == sizeof(glx_proc), "unsupported function pointer representation");
    memcpy(&symbol, &glx_proc, sizeof(symbol));
    assert(symbol);
    memcpy(&egl_swap, &symbol, sizeof(egl_swap));
    assert(egl_swap((EGLDisplay)1, (EGLSurface)1) == EGL_TRUE);
    glx_proc = glx_get_proc_arb((const GLubyte *)"eglSwapBuffersWithDamageKHR");
    memcpy(&egl_swap_damage, &glx_proc, sizeof(egl_swap_damage));
    assert(egl_swap_damage((EGLDisplay)1, (EGLSurface)1, 0, 0) == EGL_TRUE);
    {
        __eglMustCastToProperFunctionPointerType egl_proc =
            egl_get_proc("eglSwapBuffersWithDamageEXT");

        memcpy(&egl_swap_damage, &egl_proc, sizeof(egl_swap_damage));
        assert(egl_swap_damage((EGLDisplay)1, (EGLSurface)1, 0, 0) == EGL_TRUE);
    }
    symbol = dlsym(provider, "frame_pacer_test_glx_calls");
    assert(symbol);
    memcpy(&glx_calls, &symbol, sizeof(glx_calls));
    symbol = dlsym(provider, "frame_pacer_test_egl_calls");
    assert(symbol);
    memcpy(&egl_calls, &symbol, sizeof(egl_calls));
    symbol = dlsym(provider, "frame_pacer_test_gl_programs");
    assert(symbol);
    memcpy(&gl_programs, &symbol, sizeof(gl_programs));
    assert(gl_programs() == 2);

    /* Alternating back to a live context reuses its private HUD resources. */
    set_context(1, GL_FALSE);
    symbol = dlsym(RTLD_DEFAULT, "glXSwapBuffers");
    assert(symbol);
    memcpy(&glx_swap, &symbol, sizeof(glx_swap));
    glx_swap((Display *)1, 0);
    assert(gl_programs() == 2);

    /* Destroyed handles and terminated EGL displays must never retain stale
     * object names when a provider later reuses the same opaque value. */
    symbol = dlsym(RTLD_DEFAULT, "glXDestroyContext");
    assert(symbol);
    memcpy(&glx_destroy, &symbol, sizeof(glx_destroy));
    glx_destroy((Display *)1, (GLXContext)(uintptr_t)1);
    glx_swap((Display *)1, 0);
    assert(gl_programs() == 3);

    set_context(2, GL_TRUE);
    symbol = dlsym(RTLD_DEFAULT, "eglDestroyContext");
    assert(symbol);
    memcpy(&egl_destroy, &symbol, sizeof(egl_destroy));
    assert(egl_destroy((EGLDisplay)1, (EGLContext)(uintptr_t)2) == EGL_TRUE);
    assert(egl_swap((EGLDisplay)1, (EGLSurface)1) == EGL_TRUE);
    assert(gl_programs() == 4);
    symbol = dlsym(RTLD_DEFAULT, "eglTerminate");
    assert(symbol);
    memcpy(&egl_terminate, &symbol, sizeof(egl_terminate));
    assert(egl_terminate((EGLDisplay)1) == EGL_TRUE);
    assert(egl_swap((EGLDisplay)1, (EGLSurface)1) == EGL_TRUE);
    assert(gl_programs() == 5);

    symbol = dlsym(provider, "frame_pacer_test_glx_destroy_calls");
    assert(symbol);
    memcpy(&glx_destroy_calls, &symbol, sizeof(glx_destroy_calls));
    symbol = dlsym(provider, "frame_pacer_test_egl_destroy_calls");
    assert(symbol);
    memcpy(&egl_destroy_calls, &symbol, sizeof(egl_destroy_calls));
    symbol = dlsym(provider, "frame_pacer_test_egl_terminate_calls");
    assert(symbol);
    memcpy(&egl_terminate_calls, &symbol, sizeof(egl_terminate_calls));
    assert(glx_destroy_calls() == 1);
    assert(egl_destroy_calls() == 1);
    assert(egl_terminate_calls() == 1);
    assert(glx_calls() == 5);
    assert(egl_calls() == 8);
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
