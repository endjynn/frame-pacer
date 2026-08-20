#ifndef FRAME_PACER_COMPATIBILITY_H
#define FRAME_PACER_COMPATIBILITY_H

/* Compatibility rules are deliberately explicit, finite exceptions for
 * verified renderer defects. They never choose a backend or infer a frame. */
enum frame_pacer_quiet_submit_policy {
    FRAME_PACER_QUIET_SUBMIT_PACE_EVERY,
    FRAME_PACER_QUIET_SUBMIT_FORWARD,
};

enum frame_pacer_quiet_submit_policy
frame_pacer_compatibility_quiet_submit_policy(const char *executable);

#endif
