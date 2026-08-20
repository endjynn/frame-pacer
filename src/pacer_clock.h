#ifndef FRAME_PACER_CLOCK_H
#define FRAME_PACER_CLOCK_H

#include "pacer_limit.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_TARGET_FPS FRAME_PACER_DEFAULT_FPS
#define FRAME_PACER_INTERVAL_NS (UINT64_C(1000000000) / FRAME_PACER_DEFAULT_FPS)

typedef uint64_t (*frame_pacer_now_fn)(void *);
typedef int (*frame_pacer_sleep_fn)(void *, uint64_t);

struct frame_pacer_clock {
    pthread_mutex_t mutex;
    bool started;
    uint32_t fps;
    uint64_t next_deadline_ns;
};

struct frame_pacer_decision {
    uint64_t observed_ns;
    uint64_t deadline_ns;
    unsigned int interruptions;
    bool first;
    bool missed;
};

void frame_pacer_clock_init(struct frame_pacer_clock *);
void frame_pacer_clock_destroy(struct frame_pacer_clock *);
void frame_pacer_clock_wait(struct frame_pacer_clock *, uint32_t fps, frame_pacer_now_fn,
                            frame_pacer_sleep_fn, void *, struct frame_pacer_decision *);

#endif
