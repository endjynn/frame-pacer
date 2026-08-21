#include "hud_metrics_cache.h"

#include <string.h>

void frame_pacer_hud_metrics_cache_init(
    struct frame_pacer_hud_metrics_cache *cache, const char *nvml_library,
    unsigned int process_id)
{
    if (!cache)
        return;
    memset(cache, 0, sizeof(*cache));
    if (pthread_mutex_init(&cache->mutex, 0))
        return;
    frame_pacer_metrics_init(&cache->metrics, nvml_library, process_id);
    cache->initialized = true;
}

void frame_pacer_hud_metrics_cache_destroy(
    struct frame_pacer_hud_metrics_cache *cache)
{
    if (!cache || !cache->initialized)
        return;
    cache->initialized = false;
    frame_pacer_metrics_destroy(&cache->metrics);
    (void)pthread_mutex_destroy(&cache->mutex);
    memset(cache, 0, sizeof(*cache));
}

void frame_pacer_hud_metrics_cache_snapshot(
    struct frame_pacer_hud_metrics_cache *cache, uint64_t now_ns,
    struct frame_pacer_metrics_snapshot *snapshot)
{
    if (!snapshot)
        return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!cache || !cache->initialized)
        return;

    (void)pthread_mutex_lock(&cache->mutex);
    if (!cache->sample_ns || now_ns < cache->sample_ns ||
        now_ns - cache->sample_ns >= FRAME_PACER_HUD_METRICS_SAMPLE_NS) {
        frame_pacer_metrics_sample(&cache->metrics, &cache->snapshot);
        cache->sample_ns = now_ns;
    }
    *snapshot = cache->snapshot;
    (void)pthread_mutex_unlock(&cache->mutex);
}
