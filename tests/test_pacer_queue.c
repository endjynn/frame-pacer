#include "pacer_queue.h"
#include <assert.h>

int main(void)
{
    struct frame_pacer_queue_state first = {0}, second = {0};
    bool entered;
    uint64_t submits;

    assert(!frame_pacer_queue_needs_fallback(&first, 100, &entered, &submits));
    first.last_present_ns = 100;
    frame_pacer_queue_note_present(&first);
    assert(!frame_pacer_queue_needs_fallback(&first, 100 + FRAME_PACER_PRESENT_QUIET_NS,
                                             &entered, &submits));
    assert(submits == 1);
    assert(frame_pacer_queue_needs_fallback(&first, 101 + FRAME_PACER_PRESENT_QUIET_NS,
                                            &entered, &submits));
    assert(entered && submits == 2 && first.fallback_active);
    assert(frame_pacer_queue_needs_fallback(&first, 102 + FRAME_PACER_PRESENT_QUIET_NS,
                                            &entered, &submits));
    assert(!entered && submits == 3);
    frame_pacer_queue_note_present(&first);
    assert(!first.fallback_active && first.submits_since_present == 0);
    assert(!frame_pacer_queue_needs_fallback(&second, 1000, &entered, &submits));
    frame_pacer_queue_note_present(0);
    assert(!frame_pacer_queue_needs_fallback(0, 1000, &entered, &submits));
    assert(!entered && submits == 0);
    assert(!frame_pacer_queue_needs_fallback(&second, 1000, 0, &submits));
    assert(submits == 0);
    first.submits_since_present = UINT64_MAX;
    assert(frame_pacer_queue_needs_fallback(
        &first, 103 + FRAME_PACER_PRESENT_QUIET_NS, &entered, &submits));
    assert(submits == UINT64_MAX);
    return 0;
}
