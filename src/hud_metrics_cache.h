#ifndef FRAME_PACER_HUD_METRICS_CACHE_H
#define FRAME_PACER_HUD_METRICS_CACHE_H

#include "hud_metrics.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_METRICS_SAMPLE_NS UINT64_C(1000000000)

/* Serializes provider sampling and publishes one coherent snapshot per HUD
 * cadence.  Renderers may call snapshot concurrently; callers own the copy. */
struct frame_pacer_hud_metrics_cache {
    pthread_mutex_t mutex;
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    uint64_t sample_ns;
    bool initialized;
};

__attribute__((visibility("hidden")))
void frame_pacer_hud_metrics_cache_init(
    struct frame_pacer_hud_metrics_cache *, const char *nvml_library,
    unsigned int process_id);
__attribute__((visibility("hidden")))
void frame_pacer_hud_metrics_cache_destroy(
    struct frame_pacer_hud_metrics_cache *);
__attribute__((visibility("hidden")))
void frame_pacer_hud_metrics_cache_snapshot(
    struct frame_pacer_hud_metrics_cache *, uint64_t now_ns,
    struct frame_pacer_metrics_snapshot *);

#endif
