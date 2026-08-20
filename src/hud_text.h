#ifndef FRAME_PACER_HUD_TEXT_H
#define FRAME_PACER_HUD_TEXT_H

#include "hud_metrics.h"

#include <stdbool.h>
#include <stdint.h>

struct frame_pacer_hud_text {
    char lines[4][24];
    uint32_t line_count;
};

void frame_pacer_hud_text_format(
    struct frame_pacer_hud_text *,
    const struct frame_pacer_metrics_snapshot *, int fps_valid, uint32_t fps,
    uint32_t limit, bool thread_cpu_quota_configured,
    bool thread_cpu_quota_confirmed, uint32_t thread_cpu_quota);

#endif
