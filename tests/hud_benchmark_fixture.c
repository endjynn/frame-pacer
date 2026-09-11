/* Linked only into benchmark libraries, never release artifacts. */
#include "hud_text.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

void __real_frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *, const struct frame_pacer_metrics_snapshot *,
    int, uint32_t, uint32_t, bool, bool, uint32_t);
void __wrap_frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *, const struct frame_pacer_metrics_snapshot *,
    int, uint32_t, uint32_t, bool, bool, uint32_t);

void __wrap_frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *text,
    const struct frame_pacer_metrics_snapshot *metrics, int valid, uint32_t fps,
    uint32_t limit, bool configured, bool confirmed, uint32_t quota)
{
    const struct frame_pacer_metrics_snapshot fixed = {
        .available = FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP |
                     FRAME_PACER_METRIC_CPU_USE | FRAME_PACER_METRIC_CPU_TEMP |
                     FRAME_PACER_METRIC_THREAD_CPU_USE,
        .gpu_use_percent = 27,
        .gpu_temp_celsius = 62,
        .cpu_use_percent = 9,
        .cpu_temp_celsius = 81,
        .thread_cpu_percent = 16,
    };
    const char *rows = getenv("FRAME_PACER_BENCH_ROWS");
    static atomic_bool reported;
    if (!atomic_exchange_explicit(&reported, true, memory_order_relaxed))
        fprintf(stderr, "benchmark_hud_rows=%d\n",
                rows && rows[0] == '4' ? 4 : 3);
    (void)metrics;
    (void)valid;
    (void)fps;
    (void)limit;
    (void)configured;
    (void)confirmed;
    (void)quota;
    __real_frame_pacer_hud_text_format(text, &fixed, 1, 60, 60,
                                       rows && rows[0] == '4', true, 50);
}
