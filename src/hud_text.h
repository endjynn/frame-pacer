#ifndef FRAME_PACER_HUD_TEXT_H
#define FRAME_PACER_HUD_TEXT_H

#include "hud_metrics.h"

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_LINE_COUNT_MAX 4U
#define FRAME_PACER_HUD_LINE_CHARACTERS_MAX 13U

struct frame_pacer_hud_text {
    char lines[FRAME_PACER_HUD_LINE_COUNT_MAX]
              [FRAME_PACER_HUD_LINE_CHARACTERS_MAX + 1];
    uint32_t line_count;
};

void frame_pacer_hud_text_format(struct frame_pacer_hud_text *,
                                 const struct frame_pacer_metrics_snapshot *,
                                 int fps_valid, uint32_t fps, uint32_t limit,
                                 bool thread_cpu_quota_configured,
                                 bool thread_cpu_quota_confirmed,
                                 uint32_t thread_cpu_quota);

#endif
