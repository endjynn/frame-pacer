#ifndef FRAME_PACER_LOG_RETENTION_H
#define FRAME_PACER_LOG_RETENTION_H

#include <stdbool.h>

/* Logging is opt-in to avoid instrumenting unrelated global-layer clients. */
bool frame_pacer_log_enabled(void);

/* Retain the newest PID-qualified logs for one backend in directory. */
void frame_pacer_log_retention_prune(const char *directory, const char *prefix);

#endif
