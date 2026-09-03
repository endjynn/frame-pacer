#include "pacer_clock.h"

#include <errno.h>
#include <string.h>

static uint64_t add_intervals(uint64_t value, uint64_t interval, uint64_t count)
{
    return count > (UINT64_MAX - value) / interval ? UINT64_MAX
                                                   : value + count * interval;
}

void frame_pacer_clock_init(struct frame_pacer_clock *clock)
{
    if (!clock)
        return;
    memset(clock, 0, sizeof(*clock));
    clock->initialized = pthread_mutex_init(&clock->mutex, 0) == 0;
}

void frame_pacer_clock_destroy(struct frame_pacer_clock *clock)
{
    if (!clock || !clock->initialized)
        return;
    clock->initialized = false;
    (void)pthread_mutex_destroy(&clock->mutex);
}

void frame_pacer_clock_wait(struct frame_pacer_clock *clock, uint32_t fps,
                            frame_pacer_now_fn now_fn,
                            frame_pacer_sleep_fn sleep_fn, void *opaque,
                            struct frame_pacer_decision *decision)
{
    uint64_t interval_ns;
    uint64_t now_ns;

    if (decision)
        *decision = (struct frame_pacer_decision){0};
    if (!clock || !clock->initialized || !now_fn || !sleep_fn || !decision)
        return;
    if (fps < FRAME_PACER_MIN_FPS || fps > FRAME_PACER_MAX_FPS) {
        (void)pthread_mutex_lock(&clock->mutex);
        clock->started = false;
        clock->fps = FRAME_PACER_FPS_LIMIT_OFF;
        clock->next_deadline_ns = 0;
        (void)pthread_mutex_unlock(&clock->mutex);
        return;
    }
    interval_ns = UINT64_C(1000000000) / fps;

    (void)pthread_mutex_lock(&clock->mutex);
    now_ns = now_fn(opaque);
    decision->observed_ns = now_ns;

    if (!clock->started || clock->fps != fps) {
        clock->started = true;
        clock->fps = fps;
        clock->next_deadline_ns = add_intervals(now_ns, interval_ns, 1);
        decision->first = true;
        (void)pthread_mutex_unlock(&clock->mutex);
        return;
    }

    if (now_ns >= clock->next_deadline_ns) {
        uint64_t missed_intervals =
            (now_ns - clock->next_deadline_ns) / interval_ns + 1;

        clock->next_deadline_ns = add_intervals(clock->next_deadline_ns,
                                                interval_ns, missed_intervals);
        decision->missed = true;
    }

    decision->deadline_ns = clock->next_deadline_ns;
    clock->next_deadline_ns =
        add_intervals(clock->next_deadline_ns, interval_ns, 1);
    while (sleep_fn(opaque, decision->deadline_ns) == EINTR)
        ++decision->interruptions;

    (void)pthread_mutex_unlock(&clock->mutex);
}
