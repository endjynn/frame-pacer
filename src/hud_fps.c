#include "hud_fps.h"
#include <limits.h>

void frame_pacer_fps_init(struct frame_pacer_fps_tracker *tracker)
{
    (void)pthread_mutex_init(&tracker->mutex, 0);
    tracker->started = false;
    tracker->valid = false;
    tracker->sample_start_ns = 0;
    tracker->last_present_ns = 0;
    tracker->present_intervals = 0;
    tracker->fps = 0;
}

void frame_pacer_fps_destroy(struct frame_pacer_fps_tracker *tracker)
{
    (void)pthread_mutex_destroy(&tracker->mutex);
}

bool frame_pacer_fps_record_present(struct frame_pacer_fps_tracker *tracker,
                                    uint64_t now_ns, uint32_t *fps_out)
{
    uint64_t elapsed_ns;
    uint64_t fps;
    bool sampled = false;
    if (!now_ns) return false;
    (void)pthread_mutex_lock(&tracker->mutex);
    /* A long pause is not a very low frame rate.  It occurs, for example,
     * while an unfocused Wine game suppresses presentation. */
    if (!tracker->started || now_ns < tracker->last_present_ns ||
        now_ns - tracker->last_present_ns > FRAME_PACER_FPS_STALE_NS) {
        tracker->started = true;
        tracker->valid = false;
        tracker->sample_start_ns = now_ns;
        tracker->last_present_ns = now_ns;
        tracker->present_intervals = 0;
    } else {
        tracker->last_present_ns = now_ns;
        tracker->present_intervals++;
        elapsed_ns = now_ns - tracker->sample_start_ns;
        if (elapsed_ns >= FRAME_PACER_FPS_SAMPLE_NS) {
            fps = (tracker->present_intervals * UINT64_C(1000000000) + elapsed_ns / 2) / elapsed_ns;
            tracker->fps = fps > UINT32_MAX ? UINT32_MAX : (uint32_t)fps;
            tracker->valid = true;
            tracker->sample_start_ns = now_ns;
            tracker->present_intervals = 0;
            if (fps_out) *fps_out = tracker->fps;
            sampled = true;
        }
    }
    (void)pthread_mutex_unlock(&tracker->mutex);
    return sampled;
}

bool frame_pacer_fps_snapshot(struct frame_pacer_fps_tracker *tracker, uint32_t *fps_out)
{
    bool valid;
    (void)pthread_mutex_lock(&tracker->mutex);
    valid = tracker->valid;
    if (valid && fps_out) *fps_out = tracker->fps;
    (void)pthread_mutex_unlock(&tracker->mutex);
    return valid;
}
