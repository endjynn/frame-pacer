#include "hud_text.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    struct frame_pacer_hud_text text;
    struct frame_pacer_metrics_snapshot metrics = { .available = FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP | FRAME_PACER_METRIC_CPU_USE | FRAME_PACER_METRIC_CPU_TEMP | FRAME_PACER_METRIC_THREAD_CPU_USE, .gpu_use_percent = 18, .gpu_temp_celsius = 56, .cpu_use_percent = 2, .cpu_temp_celsius = 68, .thread_cpu_percent = 34 };
    frame_pacer_hud_text_format(&text, &metrics, 1, 60, 70, false, false, 0);
    assert(!strcmp(text.lines[0], "GPU  18%  56\x7f"));
    assert(!strcmp(text.lines[1], "CPU   2%  68\x7f"));
    assert(!strcmp(text.lines[2], "FPS  60\x7e  70\x7e"));
    assert(!strcmp(text.lines[3], ""));
    assert(text.line_count == 3);
    frame_pacer_hud_text_format(&text, &metrics, 1, 0, 30, true, true, 75);
    assert(!strcmp(text.lines[2], "THR  34%  75%"));
    assert(!strcmp(text.lines[3], "FPS  N/A  30\x7e"));
    assert(text.line_count == 4);
    frame_pacer_hud_text_format(&text, &(struct frame_pacer_metrics_snapshot){0}, 0, 0, 70,
                                true, false, 75);
    assert(!strcmp(text.lines[0], "GPU  N/A  N/A"));
    assert(!strcmp(text.lines[2], "THR  N/A  N/A"));
    assert(!strcmp(text.lines[3], "FPS  N/A  70\x7e"));
}
