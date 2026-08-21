#include "hud_text.h"
#include "pacer_limit.h"

#include <string.h>

static void format_column(char output[5], uint32_t value, char suffix)
{
    output[0] = value >= 100 ? (char)('0' + value / 100) : ' ';
    output[1] = value >= 10 ? (char)('0' + value / 10 % 10) : ' ';
    output[2] = (char)('0' + value % 10);
    output[3] = suffix;
    output[4] = '\0';
}

static void format_unavailable(char output[5])
{
    memcpy(output, " N/A", 5);
}

static void format_metric(unsigned int available, unsigned int flag,
                          unsigned int metric, char output[5], char suffix)
{
    /* Every metric column is four glyph cells wide: three digits plus its
     * unit.  Preserve that width for unavailable values too, otherwise the
     * following column shifts one cell left. */
    if (!(available & flag) || metric > 999) {
        format_unavailable(output);
        return;
    }
    /* Fixed-width numeric fields keep the tiny readout tabular without a
     * general layout engine.  N/A is already exactly three characters. */
    format_column(output, metric, suffix);
}

static void format_fps_count(int valid, uint32_t value, char output[5])
{
    /* Three right-aligned digits plus the compact frame glyph occupy the same
     * four cells as every other metric column. */
    if (!valid || value > FRAME_PACER_MAX_FPS)
        format_unavailable(output);
    else
        format_column(output, value, '\x7e');
}

static void format_line(char output[FRAME_PACER_HUD_LINE_CHARACTERS_MAX + 1],
                        const char label[4], const char first[5],
                        const char second[5])
{
    memcpy(output, label, 4);
    memcpy(output + 4, first, 4);
    output[8] = ' ';
    memcpy(output + 9, second, 5);
}

void frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *text,
    const struct frame_pacer_metrics_snapshot *metrics, int fps_valid,
    uint32_t fps, uint32_t limit, bool thread_cpu_quota_configured,
    bool thread_cpu_quota_confirmed, uint32_t thread_cpu_quota)
{
    char use[5], temperature[5], quota[5], fps_column[5], limit_column[5];
    const struct frame_pacer_metrics_snapshot unavailable = {0};

    if (!text) return;
    if (!metrics) metrics = &unavailable;

    format_metric(metrics->available, FRAME_PACER_METRIC_GPU_USE,
                  metrics->gpu_use_percent, use, '%');
    format_metric(metrics->available, FRAME_PACER_METRIC_GPU_TEMP,
                  metrics->gpu_temp_celsius, temperature, '\x7f');
    format_line(text->lines[0], "GPU ", use, temperature);
    format_metric(metrics->available, FRAME_PACER_METRIC_CPU_USE,
                  metrics->cpu_use_percent, use, '%');
    format_metric(metrics->available, FRAME_PACER_METRIC_CPU_TEMP,
                  metrics->cpu_temp_celsius, temperature, '\x7f');
    format_line(text->lines[1], "CPU ", use, temperature);
    if (thread_cpu_quota_configured) {
        if (thread_cpu_quota_confirmed && thread_cpu_quota <= 100)
            format_column(quota, thread_cpu_quota, '%');
        else
            format_unavailable(quota);
        format_metric(metrics->available, FRAME_PACER_METRIC_THREAD_CPU_USE,
                      metrics->thread_cpu_percent, use, '%');
        format_line(text->lines[2], "THR ", use, quota);
        text->line_count = 4;
        format_fps_count(fps_valid && fps, fps, fps_column);
        format_fps_count(1, limit, limit_column);
        format_line(text->lines[3], "FPS ", fps_column, limit_column);
    } else {
        text->line_count = 3;
        format_fps_count(fps_valid && fps, fps, fps_column);
        format_fps_count(1, limit, limit_column);
        format_line(text->lines[2], "FPS ", fps_column, limit_column);
        text->lines[3][0] = '\0';
    }
}
