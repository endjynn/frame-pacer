#ifndef FRAME_PACER_STATE_DIRECTORY_H
#define FRAME_PACER_STATE_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>

/* Resolves and creates the per-user frame-pacer state directory.  Private
 * users (controller protocol files) require mode bits no broader than 0700;
 * logs retain compatibility with readable but not group/world-writable dirs. */
__attribute__((visibility("hidden")))
bool frame_pacer_state_directory(char *output, size_t size,
                                 bool require_private);

#endif
