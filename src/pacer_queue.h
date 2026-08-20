#ifndef FRAME_PACER_QUEUE_H
#define FRAME_PACER_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_PRESENT_QUIET_NS UINT64_C(50000000)

struct frame_pacer_queue_state {
    uint64_t last_present_ns;
    uint64_t submits_since_present;
    bool has_present;
    bool fallback_active;
};

void frame_pacer_queue_note_present(struct frame_pacer_queue_state *);
bool frame_pacer_queue_needs_fallback(struct frame_pacer_queue_state *, uint64_t now_ns,
                                      bool *entered, uint64_t *submits_since_present);

#endif
