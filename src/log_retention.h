#ifndef FRAME_PACER_LOG_RETENTION_H
#define FRAME_PACER_LOG_RETENTION_H

#include <pthread.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define FRAME_PACER_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_INTERNAL
#endif

struct frame_pacer_runtime_log {
    pthread_mutex_t mutex;
    _Atomic int fd;
    uint64_t bytes;
    size_t message_capacity;
    bool capped;
};

#define FRAME_PACER_RUNTIME_LOG_INITIALIZER(capacity)                         \
    { PTHREAD_MUTEX_INITIALIZER, -1, 0, (capacity), false }

/* Logging is opt-in to avoid instrumenting unrelated global-layer clients. */
bool frame_pacer_log_enabled(void);

/* Retain the newest PID-qualified logs for one backend in directory. */
void frame_pacer_log_retention_prune(const char *directory, const char *prefix);

/* Shared opt-in runtime log lifecycle. These functions are internal to the
 * produced interposers and deliberately hidden from their dynamic ABI. */
FRAME_PACER_INTERNAL bool frame_pacer_runtime_log_open(
    struct frame_pacer_runtime_log *, const char *filename_prefix);
FRAME_PACER_INTERNAL void frame_pacer_runtime_log_vwrite(
    struct frame_pacer_runtime_log *, const char *, va_list);
FRAME_PACER_INTERNAL uint64_t frame_pacer_runtime_log_bytes(
    struct frame_pacer_runtime_log *);
FRAME_PACER_INTERNAL void frame_pacer_runtime_log_close(
    struct frame_pacer_runtime_log *);

#endif
