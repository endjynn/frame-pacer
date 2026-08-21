#ifndef FRAME_PACER_THREAD_CPU_PROTOCOL_H
#define FRAME_PACER_THREAD_CPU_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define FRAME_PACER_PROTOCOL_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_PROTOCOL_INTERNAL
#endif

FRAME_PACER_PROTOCOL_INTERNAL bool frame_pacer_thread_cpu_format_state(
    char *, size_t, bool enabled, uint32_t quota);
FRAME_PACER_PROTOCOL_INTERNAL bool frame_pacer_thread_cpu_parse_state(
    const char *, bool *enabled, uint32_t *quota);
FRAME_PACER_PROTOCOL_INTERNAL bool frame_pacer_thread_cpu_format_status(
    char *, size_t, bool confirmed, uint32_t quota);
FRAME_PACER_PROTOCOL_INTERNAL bool frame_pacer_thread_cpu_parse_confirmation(
    const char *, uint32_t quota);

#endif
