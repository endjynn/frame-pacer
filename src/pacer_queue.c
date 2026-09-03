#include "pacer_queue.h"

#include <stdint.h>

void frame_pacer_queue_note_present(struct frame_pacer_queue_state *state)
{
    if (!state)
        return;
    state->has_present = true;
    state->submits_since_present = 0;
    state->fallback_active = false;
}

bool frame_pacer_queue_needs_fallback(struct frame_pacer_queue_state *state,
                                      uint64_t now_ns, bool *entered,
                                      uint64_t *submits_since_present)
{
    bool needed;
    if (entered)
        *entered = false;
    if (submits_since_present)
        *submits_since_present = 0;
    if (!state || !entered || !submits_since_present)
        return false;
    if (!state->has_present || now_ns <= state->last_present_ns)
        return false;
    if (state->submits_since_present < UINT64_MAX)
        state->submits_since_present++;
    *submits_since_present = state->submits_since_present;
    needed = now_ns - state->last_present_ns > FRAME_PACER_PRESENT_QUIET_NS;
    if (needed && !state->fallback_active) {
        state->fallback_active = true;
        *entered = true;
    }
    return needed;
}
