#include "pacer_clock.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>

struct fake_time {
    uint64_t now_ns;
    uint64_t last_deadline_ns;
    int interruptions;
};

static uint64_t fake_now(void *opaque)
{
    return ((struct fake_time *)opaque)->now_ns;
}

static int fake_sleep(void *opaque, uint64_t deadline_ns)
{
    struct fake_time *time = opaque;

    time->last_deadline_ns = deadline_ns;
    if (time->interruptions-- > 0)
        return EINTR;
    time->now_ns = deadline_ns;
    return 0;
}

static void sequential_waits(void)
{
    struct frame_pacer_clock clock;
    struct frame_pacer_decision decision;
    struct fake_time time = {.now_ns = 100};
    uint64_t before;

    frame_pacer_clock_init(&clock);
    frame_pacer_clock_wait(&clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, &time, &decision);
    assert(decision.first);

    frame_pacer_clock_wait(&clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, &time, &decision);
    assert(time.last_deadline_ns == 100 + FRAME_PACER_INTERVAL_NS);

    time.now_ns += FRAME_PACER_INTERVAL_NS * 3;
    frame_pacer_clock_wait(&clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, &time, &decision);
    assert(decision.missed);
    assert(time.last_deadline_ns > time.now_ns - FRAME_PACER_INTERVAL_NS);

    time.interruptions = 2;
    frame_pacer_clock_wait(&clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, &time, &decision);
    assert(decision.interruptions == 2);

    frame_pacer_clock_wait(&clock, 30, fake_now, fake_sleep, &time, &decision);
    assert(decision.first);
    before = time.now_ns;
    frame_pacer_clock_wait(&clock, 30, fake_now, fake_sleep, &time, &decision);
    assert(time.last_deadline_ns == before + UINT64_C(1000000000) / 30);

    frame_pacer_clock_wait(&clock, 0, fake_now, fake_sleep, &time, &decision);
    assert(decision.first);
    assert(clock.fps == FRAME_PACER_DEFAULT_FPS);
    frame_pacer_clock_destroy(&clock);
}

struct worker_context {
    struct frame_pacer_clock *clock;
    struct fake_time *time;
    uint64_t deadline_ns;
};

static void *wait_in_worker(void *opaque)
{
    struct worker_context *context = opaque;
    struct frame_pacer_decision decision;

    frame_pacer_clock_wait(context->clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, context->time, &decision);
    context->deadline_ns = decision.deadline_ns;
    return 0;
}

static void concurrent_waits_are_serialized(void)
{
    struct frame_pacer_clock clock;
    struct fake_time time = {0};
    struct worker_context first = {&clock, &time, 0};
    struct worker_context second = {&clock, &time, 0};
    struct frame_pacer_decision decision;
    pthread_t first_thread;
    pthread_t second_thread;

    frame_pacer_clock_init(&clock);
    frame_pacer_clock_wait(&clock, FRAME_PACER_DEFAULT_FPS, fake_now,
                           fake_sleep, &time, &decision);
    assert(!pthread_create(&first_thread, 0, wait_in_worker, &first));
    assert(!pthread_create(&second_thread, 0, wait_in_worker, &second));
    assert(!pthread_join(first_thread, 0));
    assert(!pthread_join(second_thread, 0));
    assert(first.deadline_ns != second.deadline_ns);
    assert(first.deadline_ns + FRAME_PACER_INTERVAL_NS == second.deadline_ns ||
           second.deadline_ns + FRAME_PACER_INTERVAL_NS == first.deadline_ns);
    frame_pacer_clock_destroy(&clock);
}

int main(void)
{
    sequential_waits();
    concurrent_waits_are_serialized();
    return 0;
}
