#include "hud_text.h"

#include <stdio.h>

static const char *format_metric(unsigned int available, unsigned int flag,
                                 unsigned int metric, char *output, size_t size,
                                 const char *suffix)
{
    /* Every metric column is four glyph cells wide: three digits plus its
     * unit.  Preserve that width for unavailable values too, otherwise the
     * following column shifts one cell left. */
    if (!(available & flag)) {
        (void)snprintf(output, size, "%4s", "N/A");
        return output;
    }
    /* Fixed-width numeric fields keep the tiny readout tabular without a
     * general layout engine.  N/A is already exactly three characters. */
    (void)snprintf(output, size, "%3u%s", metric, suffix);
    return output;
}

static const char *format_fps_count(int valid, uint32_t value, char *output,
                                    size_t size)
{
    /* FPS has no suffix, but retains the percent/degree unit cell as a blank
     * so its digits share the same columns as the other HUD metrics. */
    if (!valid || value > 999)
        (void)snprintf(output, size, "%4s", "N/A");
    else
        (void)snprintf(output, size, "%3u\x7e", value);
    return output;
}

void frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *text,
    const struct frame_pacer_metrics_snapshot *metrics, int fps_valid,
    uint32_t fps, uint32_t limit, bool thread_cpu_quota_configured,
    bool thread_cpu_quota_confirmed, uint32_t thread_cpu_quota)
{
    char use[8], temperature[8], quota[8], fps_column[8], limit_column[8];

    (void)snprintf(text->lines[0], sizeof(text->lines[0]), "GPU %s %s",
                   format_metric(metrics->available, FRAME_PACER_METRIC_GPU_USE,
                                 metrics->gpu_use_percent, use, sizeof(use), "%"),
                   format_metric(metrics->available, FRAME_PACER_METRIC_GPU_TEMP,
                                 metrics->gpu_temp_celsius, temperature,
                                 sizeof(temperature), "\x7f"));
    (void)snprintf(text->lines[1], sizeof(text->lines[1]), "CPU %s %s",
                   format_metric(metrics->available, FRAME_PACER_METRIC_CPU_USE,
                                 metrics->cpu_use_percent, use, sizeof(use), "%"),
                   format_metric(metrics->available, FRAME_PACER_METRIC_CPU_TEMP,
                                 metrics->cpu_temp_celsius, temperature,
                                 sizeof(temperature), "\x7f"));
    if (thread_cpu_quota_configured) {
        if (thread_cpu_quota_confirmed)
            (void)snprintf(quota, sizeof(quota), "%3u%%", thread_cpu_quota);
        else
            (void)snprintf(quota, sizeof(quota), "%4s", "N/A");
        (void)snprintf(text->lines[2], sizeof(text->lines[2]), "THR %s %s",
                       format_metric(metrics->available, FRAME_PACER_METRIC_THREAD_CPU_USE,
                                     metrics->thread_cpu_percent, use, sizeof(use), "%"),
                       quota);
        text->line_count = 4;
        (void)snprintf(text->lines[3], sizeof(text->lines[3]), "FPS %s %s",
                       format_fps_count(fps_valid && fps, fps, fps_column,
                                        sizeof(fps_column)),
                       format_fps_count(1, limit, limit_column, sizeof(limit_column)));
    } else {
        text->line_count = 3;
        (void)snprintf(text->lines[2], sizeof(text->lines[2]), "FPS %s %s",
                       format_fps_count(fps_valid && fps, fps, fps_column,
                                        sizeof(fps_column)),
                       format_fps_count(1, limit, limit_column, sizeof(limit_column)));
        text->lines[3][0] = '\0';
    }
}
