#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GL/glx.h>

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef void (*frame_pacer_glx_swap_fn)(Display *, GLXDrawable);
typedef EGLBoolean (*frame_pacer_egl_swap_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*frame_pacer_egl_swap_damage_fn)(EGLDisplay, EGLSurface,
                                                     const EGLint *, EGLint);
typedef void (*frame_pacer_glx_destroy_context_fn)(Display *, GLXContext);
typedef EGLBoolean (*frame_pacer_egl_destroy_context_fn)(EGLDisplay,
                                                         EGLContext);
typedef EGLBoolean (*frame_pacer_egl_terminate_fn)(EGLDisplay);
typedef __GLXextFuncPtr (*frame_pacer_glx_get_proc_fn)(const GLubyte *);
typedef __eglMustCastToProperFunctionPointerType (*frame_pacer_egl_get_proc_fn)(
    const char *);

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay, EGLSurface, const EGLint *,
                                       EGLint);
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay, EGLSurface, const EGLint *,
                                       EGLint);

#if __SIZEOF_POINTER__ == 8
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.2.5"
#else
#define FRAME_PACER_GLIBC_DLSYM_VERSION "GLIBC_2.0"
#endif

static pthread_once_t bootstrap_once = PTHREAD_ONCE_INIT;
static pthread_once_t renderer_once = PTHREAD_ONCE_INIT;
static void *renderer;
static void *libc_reference;
static void *(*real_dlsym)(void *, const char *);
static frame_pacer_glx_swap_fn real_glx_swap;
static frame_pacer_egl_swap_fn real_egl_swap;
static frame_pacer_egl_swap_damage_fn real_egl_swap_damage_khr;
static frame_pacer_egl_swap_damage_fn real_egl_swap_damage_ext;
static frame_pacer_glx_destroy_context_fn real_glx_destroy_context;
static frame_pacer_egl_destroy_context_fn real_egl_destroy_context;
static frame_pacer_egl_terminate_fn real_egl_terminate;
static frame_pacer_glx_get_proc_fn real_glx_get_proc;
static frame_pacer_egl_get_proc_fn real_egl_get_proc;
static char shim_anchor;

static void copy_function(void *symbol, void *function, size_t size)
{
    memcpy(function, &symbol, size);
}

static void bootstrap(void)
{
    void *symbol;

    libc_reference = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
    if (!libc_reference)
        return;
    symbol = dlvsym(libc_reference, "dlsym", FRAME_PACER_GLIBC_DLSYM_VERSION);
    _Static_assert(sizeof(real_dlsym) == sizeof(symbol),
                   "unsupported function pointer representation");
    copy_function(symbol, &real_dlsym, sizeof(real_dlsym));
}

static bool mapped_shim_path(char *path, size_t path_size)
{
    FILE *maps;
    char line[PATH_MAX + 128];
    uintptr_t anchor = (uintptr_t)(void *)&shim_anchor;

    maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return false;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long start, end;
        char *file, *newline;
        int written;

        if (sscanf(line, "%lx-%lx", &start, &end) != 2 ||
            anchor < (uintptr_t)start || anchor >= (uintptr_t)end)
            continue;
        file = strchr(line, '/');
        if (!file)
            break;
        newline = strchr(file, '\n');
        if (newline)
            *newline = '\0';
        written = snprintf(path, path_size, "%s", file);
        if (written < 0 || (size_t)written >= path_size)
            break;
        (void)fclose(maps);
        return true;
    }
    (void)fclose(maps);
    return false;
}

static void load_adjacent_renderer(char *path)
{
    char *slash = strrchr(path, '/');
    size_t remaining;
    int written;

    if (!slash)
        return;
    remaining = (size_t)(path + PATH_MAX - slash - 1);
    written = snprintf(slash + 1, remaining, "libframe_pacer_gl.so");
    if (written < 0 || (size_t)written >= remaining)
        return;
    /* The directory comes from dladdr or the kernel mapping containing our
     * own shim_anchor, not an arbitrary file-supplied library path. The backend
     * basename is fixed above. Taint analysis cannot infer that provenance. */
    // NOLINTNEXTLINE(clang-analyzer-optin.taint.GenericTaint)
    renderer = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
}

static void load_renderer(void)
{
    Dl_info info;
    char path[PATH_MAX];
    void *symbol;
    int written;

    (void)pthread_once(&bootstrap_once, bootstrap);
    if (!real_dlsym)
        return;

    symbol = real_dlsym(RTLD_NEXT, "glXSwapBuffers");
    copy_function(symbol, &real_glx_swap, sizeof(real_glx_swap));
    symbol = real_dlsym(RTLD_NEXT, "eglSwapBuffers");
    copy_function(symbol, &real_egl_swap, sizeof(real_egl_swap));
    symbol = real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageKHR");
    copy_function(symbol, &real_egl_swap_damage_khr,
                  sizeof(real_egl_swap_damage_khr));
    symbol = real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageEXT");
    copy_function(symbol, &real_egl_swap_damage_ext,
                  sizeof(real_egl_swap_damage_ext));
    symbol = real_dlsym(RTLD_NEXT, "glXDestroyContext");
    copy_function(symbol, &real_glx_destroy_context,
                  sizeof(real_glx_destroy_context));
    symbol = real_dlsym(RTLD_NEXT, "eglDestroyContext");
    copy_function(symbol, &real_egl_destroy_context,
                  sizeof(real_egl_destroy_context));
    symbol = real_dlsym(RTLD_NEXT, "eglTerminate");
    copy_function(symbol, &real_egl_terminate, sizeof(real_egl_terminate));
    symbol = real_dlsym(RTLD_NEXT, "glXGetProcAddress");
    copy_function(symbol, &real_glx_get_proc, sizeof(real_glx_get_proc));
    symbol = real_dlsym(RTLD_NEXT, "eglGetProcAddress");
    copy_function(symbol, &real_egl_get_proc, sizeof(real_egl_get_proc));

    if (dladdr(&shim_anchor, &info) && info.dli_fname) {
        written = snprintf(path, sizeof(path), "%s", info.dli_fname);
        if (written >= 0 && (size_t)written < sizeof(path))
            load_adjacent_renderer(path);
    }

    /*
     * Steam pressure-vessel can stage the preload shim separately from its
     * adjacent backend.  MangoHud supports an explicit backend location for
     * this class of launcher; use the kernel's canonical mapped pathname as a
     * transparent fallback so frame-pacer keeps its single-shim launch option.
     */
    if (!renderer && mapped_shim_path(path, sizeof(path)))
        load_adjacent_renderer(path);
}

static void *renderer_symbol(const char *name)
{
    (void)pthread_once(&renderer_once, load_renderer);
    return renderer && real_dlsym ? real_dlsym(renderer, name) : 0;
}

static bool is_graphics_symbol(const char *name)
{
    return name && (!strncmp(name, "glX", 3) || !strncmp(name, "egl", 3));
}

static bool is_interposed_symbol(const char *name)
{
    return !strcmp(name, "glXSwapBuffers") || !strcmp(name, "eglSwapBuffers") ||
           !strcmp(name, "eglSwapBuffersWithDamageKHR") ||
           !strcmp(name, "eglSwapBuffersWithDamageEXT") ||
           !strcmp(name, "glXDestroyContext") ||
           !strcmp(name, "eglDestroyContext") ||
           !strcmp(name, "eglTerminate") ||
           !strcmp(name, "glXGetProcAddress") ||
           !strcmp(name, "glXGetProcAddressARB") ||
           !strcmp(name, "eglGetProcAddress");
}

/*
 * Some Unity builds resolve their GL dispatch through an internal loader before
 * requesting a presentation symbol from dlsym.  Make the adjacent backend
 * available during process startup, but leave its GL initialization lazy: the
 * backend does no graphics work until one of its presentation entry points is
 * actually called.  This keeps the shim's single adjacent-library model while
 * covering the loader order used by Papers, Please.
 */
static void __attribute__((constructor)) preload_renderer(void)
{
    (void)pthread_once(&renderer_once, load_renderer);
}

void glXSwapBuffers(Display *display, GLXDrawable drawable)
{
    frame_pacer_glx_swap_fn function = 0;
    void *symbol = renderer_symbol("glXSwapBuffers");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        function(display, drawable);
    else if (real_glx_swap)
        real_glx_swap(display, drawable);
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    frame_pacer_egl_swap_fn function = 0;
    void *symbol = renderer_symbol("eglSwapBuffers");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        return function(display, surface);
    return real_egl_swap ? real_egl_swap(display, surface) : EGL_FALSE;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    frame_pacer_egl_swap_damage_fn function = 0;
    void *symbol = renderer_symbol("eglSwapBuffersWithDamageKHR");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        return function(display, surface, rects, count);
    return real_egl_swap_damage_khr
               ? real_egl_swap_damage_khr(display, surface, rects, count)
               : EGL_FALSE;
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    frame_pacer_egl_swap_damage_fn function = 0;
    void *symbol = renderer_symbol("eglSwapBuffersWithDamageEXT");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        return function(display, surface, rects, count);
    return real_egl_swap_damage_ext
               ? real_egl_swap_damage_ext(display, surface, rects, count)
               : EGL_FALSE;
}

void glXDestroyContext(Display *display, GLXContext context)
{
    frame_pacer_glx_destroy_context_fn function = 0;
    void *symbol = renderer_symbol("glXDestroyContext");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        function(display, context);
    else if (real_glx_destroy_context)
        real_glx_destroy_context(display, context);
}

EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context)
{
    frame_pacer_egl_destroy_context_fn function = 0;
    void *symbol = renderer_symbol("eglDestroyContext");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        return function(display, context);
    return real_egl_destroy_context ? real_egl_destroy_context(display, context)
                                    : EGL_FALSE;
}

EGLBoolean eglTerminate(EGLDisplay display)
{
    frame_pacer_egl_terminate_fn function = 0;
    void *symbol = renderer_symbol("eglTerminate");

    copy_function(symbol, &function, sizeof(function));
    if (function)
        return function(display);
    return real_egl_terminate ? real_egl_terminate(display) : EGL_FALSE;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    frame_pacer_glx_get_proc_fn function = 0;
    void *symbol = renderer_symbol("glXGetProcAddress");

    copy_function(symbol, &function, sizeof(function));
    return function ? function(name)
                    : (real_glx_get_proc ? real_glx_get_proc(name) : 0);
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name)
{
    return glXGetProcAddress(name);
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    frame_pacer_egl_get_proc_fn function = 0;
    void *symbol = renderer_symbol("eglGetProcAddress");

    copy_function(symbol, &function, sizeof(function));
    return function ? function(name)
                    : (real_egl_get_proc ? real_egl_get_proc(name) : 0);
}

void *dlsym(void *handle, const char *name)
{
    void *symbol;

    (void)pthread_once(&bootstrap_once, bootstrap);
    if (!real_dlsym)
        return 0;

    /*
     * Match MangoHud's ordering: preserve the caller's requested lookup before
     * considering an override.  Unity resolves setup entry points (such as
     * glXMakeCurrent) before it asks for the presentation entry point.  Loading
     * the private renderer on that first GLX/EGL lookup makes the later
     * resolver path available without changing the result for unrelated GL
     * functions.
     */
    symbol = real_dlsym(handle, name);
    if (!symbol || !is_graphics_symbol(name))
        return symbol;

    if (is_interposed_symbol(name)) {
        void *replacement = renderer_symbol(name);
        if (replacement)
            return replacement;
    } else {
        (void)renderer_symbol(name);
    }

    return symbol;
}

static void __attribute__((destructor)) unload_renderer(void)
{
    if (renderer) {
        (void)dlclose(renderer);
        renderer = 0;
    }
    if (libc_reference) {
        (void)dlclose(libc_reference);
        libc_reference = 0;
    }
}
