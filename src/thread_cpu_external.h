#ifndef FRAME_PACER_THREAD_CPU_EXTERNAL_H
#define FRAME_PACER_THREAD_CPU_EXTERNAL_H

#include "thread_cpu_quota.h"

#include <stdbool.h>
#include <stdint.h>

struct frame_pacer_systemd;

#if defined(__GNUC__)
#define FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL \
    __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL
#endif

FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL bool
frame_pacer_thread_cpu_external_write(const char *, bool, uint32_t);
FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL bool
frame_pacer_thread_cpu_external_confirmed(const char *, uint32_t);
FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL void
frame_pacer_thread_cpu_external_wait_off(const char *);
FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL void
frame_pacer_thread_cpu_external_reap(struct frame_pacer_thread_cpu_quota *);
FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL bool
frame_pacer_thread_cpu_external_start_service(
    struct frame_pacer_thread_cpu_quota *, struct frame_pacer_systemd *,
    const char *, uint32_t);
FRAME_PACER_THREAD_CPU_EXTERNAL_INTERNAL bool
frame_pacer_thread_cpu_external_start_native(
    struct frame_pacer_thread_cpu_quota *, const char *, uint32_t,
    const char **failure_stage);

#endif
