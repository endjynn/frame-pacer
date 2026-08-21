#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include "gl_hud_renderer.h"
#include "gl_pacer_dispatch.h"
#include "hud_fps.h"
#include "hud_metrics_cache.h"
#include "hud_text.h"
#include "log_retention.h"
#include "pacer_clock.h"
#include "pacer_limit.h"
#include "thread_cpu_quota.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_once_t metrics_once = PTHREAD_ONCE_INIT;
static void logmsg(const char *format, ...);
static void hud_log(const char *message) { logmsg("%s", message); }
static struct frame_pacer_clock clock_;
static struct frame_pacer_limit limit_;
static struct frame_pacer_thread_cpu_quota thread_cpu_quota_;
static void thread_cpu_quota_log(const char *message) { logmsg("%s", message); }
static struct frame_pacer_fps_tracker fps_;
static struct frame_pacer_hud_metrics_cache metrics_;
static struct frame_pacer_gl_dispatch dispatch_;
static struct frame_pacer_hud_vertices hud_vertices_;
static struct frame_pacer_gl_hud_renderer hud_renderer_ =
    FRAME_PACER_GL_HUD_RENDERER_INITIALIZER(&hud_vertices_, hud_log);
#define next_glx_swap dispatch_.next_glx_swap
#define next_egl_swap dispatch_.next_egl_swap
#define next_egl_swap_damage_khr dispatch_.next_egl_swap_damage_khr
#define next_egl_swap_damage_ext dispatch_.next_egl_swap_damage_ext
#define real_dlsym dispatch_.real_dlsym
#define next_glx_get_proc dispatch_.next_glx_get_proc
#define next_egl_get_proc dispatch_.next_egl_get_proc
#define next_egl_destroy_context dispatch_.next_egl_destroy_context
#define next_egl_terminate dispatch_.next_egl_terminate
#define next_glx_destroy_context dispatch_.next_glx_destroy_context
static struct frame_pacer_runtime_log runtime_log =
    FRAME_PACER_RUNTIME_LOG_INITIALIZER(512);
static uint64_t swaps;
static unsigned int resolver_requests;
static bool hud_available;
static bool backend_initialized;
static bool metrics_initialized;

static uint64_t now_ns(void *unused)
{
    struct timespec time;
    (void)unused;
    return clock_gettime(CLOCK_MONOTONIC, &time) ? 0 :
        (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static int sleep_until(void *unused, uint64_t deadline)
{
    struct timespec time = {
        .tv_sec = (time_t)(deadline / UINT64_C(1000000000)),
        .tv_nsec = (long)(deadline % UINT64_C(1000000000)),
    };
    (void)unused;
    return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, 0);
}

static void logmsg(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    frame_pacer_runtime_log_vwrite(&runtime_log, format, arguments);
    va_end(arguments);
}

static void init_log(void)
{
    (void)frame_pacer_runtime_log_open(&runtime_log, "frame-pacer-gl-");
}

static void init(void)
{
    frame_pacer_clock_init(&clock_);
    frame_pacer_limit_init(&limit_);
    frame_pacer_thread_cpu_quota_init(&thread_cpu_quota_);
    frame_pacer_thread_cpu_quota_set_logger(&thread_cpu_quota_, thread_cpu_quota_log);
    frame_pacer_fps_init(&fps_);
    init_log();
    (void)frame_pacer_gl_dispatch_init(&dispatch_);
    hud_available = dispatch_.hud_available;
    logmsg("frame-pacer: GL interposer init pid=%ld arch=%zu dlsym=%d glx=%d egl=%d hud=%d\n",
           (long)getpid(), sizeof(void *) * 8, real_dlsym != 0, next_glx_swap != 0,
           next_egl_swap != 0, hud_available);
    backend_initialized = true;
}

/* Metrics loading calls dlopen/dlsym.  It must run only after init_once has
 * completed, otherwise this interposer can recurse into dlsym and wait on its
 * own in-progress once-control. */
static void init_metrics(void)
{
    frame_pacer_hud_metrics_cache_init(&metrics_, 0,
                                       (unsigned int)getpid());
    metrics_initialized = true;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count);
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count);
void glXDestroyContext(Display *display, GLXContext context);
EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context);
EGLBoolean eglTerminate(EGLDisplay display);

static void *interposed_symbol(const char *name)
{
    void *address = 0;
    frame_pacer_glx_swap_fn glx_swap;
    frame_pacer_egl_swap_fn egl_swap;

    if (!name) return 0;
    if (!strcmp(name, "glXSwapBuffers")) {
        glx_swap = glXSwapBuffers;
        _Static_assert(sizeof(address) == sizeof(glx_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &glx_swap, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffers")) {
        egl_swap = eglSwapBuffers;
        _Static_assert(sizeof(address) == sizeof(egl_swap),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_swap, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffersWithDamageKHR")) {
        frame_pacer_egl_swap_damage_fn egl_damage =
            eglSwapBuffersWithDamageKHR;

        _Static_assert(sizeof(address) == sizeof(egl_damage),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_damage, sizeof(address));
    } else if (!strcmp(name, "eglSwapBuffersWithDamageEXT")) {
        frame_pacer_egl_swap_damage_fn egl_damage =
            eglSwapBuffersWithDamageEXT;

        _Static_assert(sizeof(address) == sizeof(egl_damage),
                       "unsupported function pointer representation");
        memcpy(&address, &egl_damage, sizeof(address));
    } else if (!strcmp(name, "glXDestroyContext")) {
        frame_pacer_glx_destroy_context_fn destroy = glXDestroyContext;

        _Static_assert(sizeof(address) == sizeof(destroy),
                       "unsupported function pointer representation");
        memcpy(&address, &destroy, sizeof(address));
    } else if (!strcmp(name, "eglDestroyContext")) {
        frame_pacer_egl_destroy_context_fn destroy = eglDestroyContext;

        _Static_assert(sizeof(address) == sizeof(destroy),
                       "unsupported function pointer representation");
        memcpy(&address, &destroy, sizeof(address));
    } else if (!strcmp(name, "eglTerminate")) {
        frame_pacer_egl_terminate_fn terminate = eglTerminate;

        _Static_assert(sizeof(address) == sizeof(terminate),
                       "unsupported function pointer representation");
        memcpy(&address, &terminate, sizeof(address));
    }
    return address;
}

static void pace(const char *api)
{
    struct frame_pacer_decision decision;
    bool changed;
    uint32_t fps = frame_pacer_limit_poll(&limit_, now_ns(0), &changed);

    if (changed) logmsg("frame-pacer: GL FPS limit changed to %u\n", fps);
    frame_pacer_clock_wait(&clock_, fps, now_ns, sleep_until, 0, &decision);
    (void)__atomic_add_fetch(&swaps, 1, __ATOMIC_RELAXED);
    logmsg("frame-pacer: %s first=%d missed=%d now=%" PRIu64
           " deadline=%" PRIu64 " eintr=%u cap=%u\n", api, decision.first,
           decision.missed, decision.observed_ns, decision.deadline_ns,
           decision.interruptions, fps);
}

static void render_hud(EGLDisplay egl_display, EGLSurface egl_surface,
                       Display *glx_display, GLXDrawable glx_drawable)
{
    struct frame_pacer_metrics_snapshot metrics;
    struct frame_pacer_hud_text text;
    GLint viewport[4];
    uint32_t fps, limit, thread_cpu_quota;
    bool fps_valid, thread_cpu_quota_configured;

    if (!hud_available) return;
    (void)frame_pacer_limit_poll(&limit_, now_ns(0), 0);
    if (!frame_pacer_limit_hud_enabled(&limit_)) return;
    if ((glx_display && (!dispatch_.next_glx_current_context ||
                         !dispatch_.next_glx_current_context())) ||
        (!glx_display && (!dispatch_.next_egl_current_context ||
                          !dispatch_.next_egl_current_context())))
        return;
    dispatch_.gl_get_integer(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    (void)pthread_once(&metrics_once, init_metrics);
    limit = frame_pacer_limit_poll(&limit_, now_ns(0), 0);
    thread_cpu_quota = frame_pacer_limit_thread_cpu_quota(
        &limit_, &thread_cpu_quota_configured);
    frame_pacer_thread_cpu_quota_publish(&thread_cpu_quota_,
                                         thread_cpu_quota_configured,
                                         thread_cpu_quota);
    frame_pacer_hud_metrics_cache_snapshot(&metrics_, now_ns(0), &metrics);
    fps_valid = frame_pacer_fps_snapshot(&fps_, &fps);
    frame_pacer_hud_text_format(
        &text, &metrics, fps_valid, fps, limit, thread_cpu_quota_configured,
        frame_pacer_thread_cpu_quota_confirmed(&thread_cpu_quota_, 0),
        thread_cpu_quota);
    frame_pacer_gl_hud_render(&hud_renderer_, &dispatch_, &text, egl_display,
                              egl_surface, glx_display, glx_drawable);
}

void glXSwapBuffers(Display *display, GLXDrawable drawable)
{
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next_glx_swap) return;
    render_hud(EGL_NO_DISPLAY, EGL_NO_SURFACE, display, drawable);
    pace("glXSwapBuffers");
    next_glx_swap(display, drawable);
    accepted = now_ns(0);
    if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    EGLBoolean result;
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next_egl_swap) return EGL_FALSE;
    render_hud(display, surface, 0, 0);
    pace("eglSwapBuffers");
    result = next_egl_swap(display, surface);
    if (result == EGL_TRUE) {
        accepted = now_ns(0);
        if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
    }
    return result;
}

static EGLBoolean swap_damage(const char *api, frame_pacer_egl_swap_damage_fn next,
                              EGLDisplay display, EGLSurface surface,
                              const EGLint *rects, EGLint count)
{
    EGLBoolean result;
    uint64_t accepted;

    (void)pthread_once(&init_once, init);
    if (!next) return EGL_FALSE;
    render_hud(display, surface, 0, 0);
    pace(api);
    result = next(display, surface, rects, count);
    if (result == EGL_TRUE) {
        accepted = now_ns(0);
        if (accepted) (void)frame_pacer_fps_record_present(&fps_, accepted, 0);
    }
    return result;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    return swap_damage("eglSwapBuffersWithDamageKHR", next_egl_swap_damage_khr,
                       display, surface, rects, count);
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                       const EGLint *rects, EGLint count)
{
    return swap_damage("eglSwapBuffersWithDamageEXT", next_egl_swap_damage_ext,
                       display, surface, rects, count);
}

void glXDestroyContext(Display *display, GLXContext context)
{
    (void)pthread_once(&init_once, init);
    if (!next_glx_destroy_context) return;
    next_glx_destroy_context(display, context);
    frame_pacer_gl_hud_forget(&hud_renderer_, FRAME_PACER_GL_CONTEXT_GLX,
                              display, context, false);
}

EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context)
{
    EGLBoolean result;

    (void)pthread_once(&init_once, init);
    if (!next_egl_destroy_context) return EGL_FALSE;
    result = next_egl_destroy_context(display, context);
    if (result == EGL_TRUE) {
        frame_pacer_gl_hud_forget(&hud_renderer_, FRAME_PACER_GL_CONTEXT_EGL,
                                  display, context, false);
    }
    return result;
}

EGLBoolean eglTerminate(EGLDisplay display)
{
    EGLBoolean result;

    (void)pthread_once(&init_once, init);
    if (!next_egl_terminate) return EGL_FALSE;
    result = next_egl_terminate(display);
    if (result == EGL_TRUE) {
        frame_pacer_gl_hud_forget(&hud_renderer_, FRAME_PACER_GL_CONTEXT_EGL,
                                  display, 0, true);
    }
    return result;
}

void *dlsym(void *handle, const char *name)
{
    void *replacement;

    (void)pthread_once(&init_once, init);
    if (__atomic_fetch_add(&resolver_requests, 1, __ATOMIC_RELAXED) < 64)
        logmsg("frame-pacer: dlsym requested %s\n", name);
    replacement = interposed_symbol(name);
    if (replacement) {
        logmsg("frame-pacer: dlsym substituted %s\n", name);
        return replacement;
    }
    return real_dlsym ? real_dlsym(handle, name) : 0;
}

__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    void *replacement;
    __GLXextFuncPtr function = 0;

    (void)pthread_once(&init_once, init);
    if (name &&
        __atomic_fetch_add(&resolver_requests, 1, __ATOMIC_RELAXED) < 64)
        logmsg("frame-pacer: glXGetProcAddress requested %s\n", name);
    replacement = interposed_symbol((const char *)name);
    if (replacement) {
        memcpy(&function, &replacement, sizeof(function));
        logmsg("frame-pacer: glXGetProcAddress substituted %s\n", name);
        return function;
    }
    return next_glx_get_proc ? next_glx_get_proc(name) : 0;
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name)
{
    return glXGetProcAddress(name);
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    void *replacement;
    __eglMustCastToProperFunctionPointerType function = 0;

    (void)pthread_once(&init_once, init);
    if (name &&
        __atomic_fetch_add(&resolver_requests, 1, __ATOMIC_RELAXED) < 64)
        logmsg("frame-pacer: eglGetProcAddress requested %s\n", name);
    replacement = interposed_symbol(name);
    if (replacement) {
        memcpy(&function, &replacement, sizeof(function));
        logmsg("frame-pacer: eglGetProcAddress substituted %s\n", name);
        return function;
    }
    return next_egl_get_proc ? next_egl_get_proc(name) : 0;
}

static void __attribute__((destructor)) done(void)
{

    logmsg("frame-pacer: GL shutdown swaps=%" PRIu64 " log_bytes=%" PRIu64 "\n",
           __atomic_load_n(&swaps, __ATOMIC_RELAXED),
           frame_pacer_runtime_log_bytes(&runtime_log));
    frame_pacer_runtime_log_close(&runtime_log);
    frame_pacer_gl_hud_renderer_destroy(&hud_renderer_);
    if (metrics_initialized)
        frame_pacer_hud_metrics_cache_destroy(&metrics_);
    if (backend_initialized) {
        frame_pacer_thread_cpu_quota_destroy(&thread_cpu_quota_);
        frame_pacer_fps_destroy(&fps_);
        frame_pacer_limit_destroy(&limit_);
        frame_pacer_clock_destroy(&clock_);
    }
    frame_pacer_gl_dispatch_destroy(&dispatch_);
}
