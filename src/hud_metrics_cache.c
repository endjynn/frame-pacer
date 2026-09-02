#include "hud_metrics_cache.h"

#include <string.h>

static void *metrics_worker(void *argument)
{
    struct frame_pacer_hud_metrics_cache *cache = argument;

    for (;;) {
        struct frame_pacer_metrics_snapshot snapshot = {0};
        bool reset;

        (void)pthread_mutex_lock(&cache->mutex);
        while (!cache->request_pending && !cache->stop)
            (void)pthread_cond_wait(&cache->condition, &cache->mutex);
        if (cache->stop) {
            (void)pthread_mutex_unlock(&cache->mutex);
            break;
        }
        cache->request_pending = false;
        reset = cache->reset_pending;
        cache->reset_pending = false;
        cache->sample_in_progress = true;
        (void)pthread_mutex_unlock(&cache->mutex);

#ifdef FRAME_PACER_TEST
        if (cache->test_sample) {
            if (reset && cache->test_reset)
                cache->test_reset(cache->test_context);
            cache->test_sample(cache->test_context, &snapshot);
        } else
#endif
        {
            if (!cache->metrics_initialized) {
                frame_pacer_metrics_init(&cache->metrics, 0,
                                         cache->process_id);
                cache->metrics_initialized = true;
            }
            if (reset)
                frame_pacer_metrics_reset_utilization(&cache->metrics);
            frame_pacer_metrics_sample(&cache->metrics, &snapshot);
        }

        (void)pthread_mutex_lock(&cache->mutex);
        cache->snapshot = snapshot;
        cache->sample_in_progress = false;
        (void)pthread_cond_broadcast(&cache->condition);
        (void)pthread_mutex_unlock(&cache->mutex);
    }
    return 0;
}

void frame_pacer_hud_metrics_cache_init(
    struct frame_pacer_hud_metrics_cache *cache, unsigned int process_id)
{
    if (!cache)
        return;
    memset(cache, 0, sizeof(*cache));
    if (pthread_mutex_init(&cache->mutex, 0))
        return;
    if (pthread_cond_init(&cache->condition, 0)) {
        (void)pthread_mutex_destroy(&cache->mutex);
        memset(cache, 0, sizeof(*cache));
        return;
    }
    cache->process_id = process_id;
    cache->initialized = true;
}

void frame_pacer_hud_metrics_cache_destroy(
    struct frame_pacer_hud_metrics_cache *cache)
{
    bool join_worker;

    if (!cache || !cache->initialized)
        return;
    (void)pthread_mutex_lock(&cache->mutex);
    cache->initialized = false;
    cache->stop = true;
    join_worker = cache->worker_started;
    (void)pthread_cond_broadcast(&cache->condition);
    (void)pthread_mutex_unlock(&cache->mutex);
    if (join_worker)
        (void)pthread_join(cache->worker, 0);
    if (cache->metrics_initialized)
        frame_pacer_metrics_destroy(&cache->metrics);
    (void)pthread_cond_destroy(&cache->condition);
    (void)pthread_mutex_destroy(&cache->mutex);
    memset(cache, 0, sizeof(*cache));
}

void frame_pacer_hud_metrics_cache_snapshot(
    struct frame_pacer_hud_metrics_cache *cache, uint64_t now_ns,
    struct frame_pacer_metrics_snapshot *snapshot)
{
    bool request = false;

    if (!snapshot)
        return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!cache || !cache->initialized)
        return;

    (void)pthread_mutex_lock(&cache->mutex);
    if (!cache->request_time_valid) {
        request = true;
    } else if (now_ns < cache->request_ns) {
        request = true;
        cache->reset_pending = true;
    } else if (now_ns - cache->request_ns >=
               FRAME_PACER_HUD_METRICS_SAMPLE_NS) {
        request = true;
        if (now_ns - cache->request_ns >= FRAME_PACER_HUD_METRICS_RESET_NS)
            cache->reset_pending = true;
    }
    if (request) {
        cache->request_ns = now_ns;
        cache->request_time_valid = true;
        cache->request_pending = true;
        if (!cache->worker_started && !cache->worker_failed) {
#ifdef FRAME_PACER_TEST
            int created = cache->test_thread_failure ? 1 :
                pthread_create(&cache->worker, 0, metrics_worker, cache);
#else
            int created = pthread_create(&cache->worker, 0, metrics_worker,
                                         cache);
#endif
            if (created) {
                cache->worker_failed = true;
                cache->request_pending = false;
            } else {
                cache->worker_started = true;
            }
        }
        if (cache->worker_started)
            (void)pthread_cond_signal(&cache->condition);
    }
    *snapshot = cache->snapshot;
    (void)pthread_mutex_unlock(&cache->mutex);
}

#ifdef FRAME_PACER_TEST
void frame_pacer_hud_metrics_cache_test_set_sampler(
    struct frame_pacer_hud_metrics_cache *cache,
    frame_pacer_hud_metrics_cache_test_sample_fn sample,
    frame_pacer_hud_metrics_cache_test_reset_fn reset, void *context)
{
    if (!cache || !cache->initialized || cache->worker_started)
        return;
    cache->test_sample = sample;
    cache->test_reset = reset;
    cache->test_context = context;
}

void frame_pacer_hud_metrics_cache_test_fail_thread_creation(
    struct frame_pacer_hud_metrics_cache *cache)
{
    if (!cache || !cache->initialized || cache->worker_started)
        return;
    cache->test_thread_failure = true;
}
#endif
