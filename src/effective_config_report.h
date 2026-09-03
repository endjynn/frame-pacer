#ifndef FRAME_PACER_EFFECTIVE_CONFIG_REPORT_H
#define FRAME_PACER_EFFECTIVE_CONFIG_REPORT_H

#include "pacer_limit.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define FRAME_PACER_REPORT_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_REPORT_INTERNAL
#endif

#define FRAME_PACER_EFFECTIVE_REPORT_CAPACITY 512U

enum frame_pacer_report_backend {
    FRAME_PACER_REPORT_VULKAN,
    FRAME_PACER_REPORT_GLX,
    FRAME_PACER_REPORT_EGL,
    FRAME_PACER_REPORT_BACKEND_COUNT
};

struct frame_pacer_effective_reporter {
    pthread_mutex_t mutex;
    _Atomic uint64_t reported[FRAME_PACER_REPORT_BACKEND_COUNT];
};

#define FRAME_PACER_EFFECTIVE_REPORTER_INITIALIZER                             \
    {PTHREAD_MUTEX_INITIALIZER, {0, 0, 0}}

typedef void (*frame_pacer_report_write_fn)(void *, const char *);

FRAME_PACER_REPORT_INTERNAL size_t frame_pacer_effective_report_format(
    char *, size_t, const struct frame_pacer_effective_config *,
    enum frame_pacer_report_backend, bool startup);
FRAME_PACER_REPORT_INTERNAL bool frame_pacer_effective_report_if_due(
    struct frame_pacer_effective_reporter *, struct frame_pacer_limit *,
    enum frame_pacer_report_backend, frame_pacer_report_write_fn, void *);

#endif
