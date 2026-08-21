#include "hud_fps.h"
#include "pacer_clock.h"
#include <assert.h>

static void cadence(struct frame_pacer_fps_tracker *tracker, uint64_t *now, unsigned frames, uint64_t interval)
{
    unsigned i;
    for (i = 0; i < frames; ++i) {
        *now += interval;
        (void)frame_pacer_fps_record_present(tracker, *now, 0);
    }
}

static void normal_cadence(void)
{
    struct frame_pacer_fps_tracker tracker;
    uint32_t fps = 0;
    uint64_t now = 1;
    frame_pacer_fps_init(&tracker);
    assert(!frame_pacer_fps_snapshot(&tracker, &fps));
    assert(!frame_pacer_fps_record_present(&tracker, now, &fps));
    cadence(&tracker, &now, 40, FRAME_PACER_INTERVAL_NS);
    assert(frame_pacer_fps_snapshot(&tracker, &fps));
    assert(fps == FRAME_PACER_TARGET_FPS);
    frame_pacer_fps_destroy(&tracker);
}

static void lower_and_higher_cadence(void)
{
    struct frame_pacer_fps_tracker tracker;
    uint32_t fps = 0;
    uint64_t now = 1;
    frame_pacer_fps_init(&tracker);
    (void)frame_pacer_fps_record_present(&tracker, now, 0);
    cadence(&tracker, &now, 16, UINT64_C(33333333));
    assert(frame_pacer_fps_snapshot(&tracker, &fps));
    assert(fps == 30);
    frame_pacer_fps_destroy(&tracker);

    now = 1;
    frame_pacer_fps_init(&tracker);
    (void)frame_pacer_fps_record_present(&tracker, now, 0);
    cadence(&tracker, &now, 120, UINT64_C(4166667));
    assert(frame_pacer_fps_snapshot(&tracker, &fps));
    assert(fps == 240);
    frame_pacer_fps_destroy(&tracker);
}

static void stale_presentation_is_not_fabricated(void)
{
    struct frame_pacer_fps_tracker tracker;
    uint32_t fps = 0;
    uint64_t now = 1;
    frame_pacer_fps_init(&tracker);
    (void)frame_pacer_fps_record_present(&tracker, now, 0);
    cadence(&tracker, &now, 40, FRAME_PACER_INTERVAL_NS);
    assert(frame_pacer_fps_snapshot(&tracker, &fps) && fps == FRAME_PACER_TARGET_FPS);
    now += FRAME_PACER_FPS_STALE_NS + 1;
    assert(!frame_pacer_fps_record_present(&tracker, now, &fps));
    assert(!frame_pacer_fps_snapshot(&tracker, &fps));
    cadence(&tracker, &now, 40, FRAME_PACER_INTERVAL_NS);
    assert(frame_pacer_fps_snapshot(&tracker, &fps) && fps == FRAME_PACER_TARGET_FPS);
    frame_pacer_fps_destroy(&tracker);
}

static void extreme_interval_count_saturates(void)
{
    struct frame_pacer_fps_tracker tracker;
    uint32_t fps = 0;
    uint64_t now = FRAME_PACER_FPS_SAMPLE_NS + 1;

    frame_pacer_fps_init(&tracker);
    tracker.started = true;
    tracker.sample_start_ns = 1;
    tracker.last_present_ns = now - 1;
    tracker.present_intervals = UINT64_MAX;
    assert(frame_pacer_fps_record_present(&tracker, now, &fps));
    assert(fps == UINT32_MAX);
    frame_pacer_fps_destroy(&tracker);
    frame_pacer_fps_destroy(&tracker);
    frame_pacer_fps_init(0);
    assert(!frame_pacer_fps_record_present(0, now, &fps));
    assert(!frame_pacer_fps_snapshot(0, &fps));
}

int main(void)
{
    normal_cadence();
    lower_and_higher_cadence();
    stale_presentation_is_not_fabricated();
    extreme_interval_count_saturates();
}
