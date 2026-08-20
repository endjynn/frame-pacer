#ifndef FRAME_PACER_HUD_FPS_H
#define FRAME_PACER_HUD_FPS_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_FPS_SAMPLE_NS UINT64_C(500000000)
#define FRAME_PACER_FPS_STALE_NS UINT64_C(2000000000)

/* The HUD reports accepted present-call cadence, not a requested limiter rate
 * or render/submit-loop rate.  Call record_present only after the presentation
 * backend has accepted the frame. */
struct frame_pacer_fps_tracker {
    pthread_mutex_t mutex;
    bool started;
    bool valid;
    uint64_t sample_start_ns;
    uint64_t last_present_ns;
    uint64_t present_intervals;
    uint32_t fps;
};

void frame_pacer_fps_init(struct frame_pacer_fps_tracker *);
void frame_pacer_fps_destroy(struct frame_pacer_fps_tracker *);
/* Returns true only when a complete new sample has been produced. */
bool frame_pacer_fps_record_present(struct frame_pacer_fps_tracker *,
                                    uint64_t now_ns, uint32_t *fps_out);
bool frame_pacer_fps_snapshot(struct frame_pacer_fps_tracker *, uint32_t *fps_out);

#endif
