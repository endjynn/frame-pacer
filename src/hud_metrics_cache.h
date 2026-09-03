#ifndef FRAME_PACER_HUD_METRICS_CACHE_H
#define FRAME_PACER_HUD_METRICS_CACHE_H

#include "hud_metrics.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_METRICS_SAMPLE_NS UINT64_C(1000000000)
#define FRAME_PACER_HUD_METRICS_RESET_NS UINT64_C(5000000000)

#ifdef FRAME_PACER_TEST
typedef void (*frame_pacer_hud_metrics_cache_test_sample_fn)(
    void *, struct frame_pacer_metrics_snapshot *);
typedef void (*frame_pacer_hud_metrics_cache_test_reset_fn)(void *);
#endif

/* Publishes asynchronously sampled telemetry. Renderers may call snapshot
 * concurrently and receive their own coherent copy without performing I/O. */
struct frame_pacer_hud_metrics_cache {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    uint64_t request_ns;
    unsigned int process_id;
    bool initialized;
    bool worker_started;
    bool worker_failed;
    bool stop;
    bool request_pending;
    bool reset_pending;
    bool request_time_valid;
    bool sample_in_progress;
    bool metrics_initialized;
#ifdef FRAME_PACER_TEST
    frame_pacer_hud_metrics_cache_test_sample_fn test_sample;
    frame_pacer_hud_metrics_cache_test_reset_fn test_reset;
    void *test_context;
    bool test_thread_failure;
#endif
};

__attribute__((visibility("hidden"))) void
frame_pacer_hud_metrics_cache_init(struct frame_pacer_hud_metrics_cache *,
                                   unsigned int process_id);
__attribute__((visibility("hidden"))) void
frame_pacer_hud_metrics_cache_destroy(struct frame_pacer_hud_metrics_cache *);
__attribute__((visibility("hidden"))) void
frame_pacer_hud_metrics_cache_snapshot(struct frame_pacer_hud_metrics_cache *,
                                       uint64_t now_ns,
                                       struct frame_pacer_metrics_snapshot *);

#ifdef FRAME_PACER_TEST
void frame_pacer_hud_metrics_cache_test_set_sampler(
    struct frame_pacer_hud_metrics_cache *,
    frame_pacer_hud_metrics_cache_test_sample_fn,
    frame_pacer_hud_metrics_cache_test_reset_fn, void *);
void frame_pacer_hud_metrics_cache_test_fail_thread_creation(
    struct frame_pacer_hud_metrics_cache *);
#endif

#endif
