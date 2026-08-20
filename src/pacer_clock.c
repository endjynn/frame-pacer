#include "pacer_clock.h"

#include <errno.h>

static uint32_t valid_fps(uint32_t fps)
{
    return fps >= FRAME_PACER_MIN_FPS && fps <= FRAME_PACER_MAX_FPS
               ? fps
               : FRAME_PACER_DEFAULT_FPS;
}

void frame_pacer_clock_init(struct frame_pacer_clock *clock)
{
    (void)pthread_mutex_init(&clock->mutex, 0);
    clock->started = false;
    clock->fps = 0;
    clock->next_deadline_ns = 0;
}

void frame_pacer_clock_destroy(struct frame_pacer_clock *clock)
{
    (void)pthread_mutex_destroy(&clock->mutex);
}

void frame_pacer_clock_wait(struct frame_pacer_clock *clock, uint32_t fps,
                            frame_pacer_now_fn now_fn, frame_pacer_sleep_fn sleep_fn,
                            void *opaque, struct frame_pacer_decision *decision)
{
    uint64_t interval_ns;
    uint64_t now_ns;

    *decision = (struct frame_pacer_decision){0};
    fps = valid_fps(fps);
    interval_ns = UINT64_C(1000000000) / fps;

    (void)pthread_mutex_lock(&clock->mutex);
    now_ns = now_fn(opaque);
    decision->observed_ns = now_ns;

    if (!clock->started || clock->fps != fps) {
        clock->started = true;
        clock->fps = fps;
        clock->next_deadline_ns = now_ns + interval_ns;
        decision->first = true;
        (void)pthread_mutex_unlock(&clock->mutex);
        return;
    }

    if (now_ns >= clock->next_deadline_ns) {
        uint64_t missed_intervals =
            (now_ns - clock->next_deadline_ns) / interval_ns + 1;

        clock->next_deadline_ns += missed_intervals * interval_ns;
        decision->missed = true;
    }

    decision->deadline_ns = clock->next_deadline_ns;
    clock->next_deadline_ns += interval_ns;
    while (sleep_fn(opaque, decision->deadline_ns) == EINTR)
        ++decision->interruptions;

    (void)pthread_mutex_unlock(&clock->mutex);
}
