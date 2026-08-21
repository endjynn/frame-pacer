#include "hud_metrics_cache.h"

#include <assert.h>
#include <pthread.h>

static void *snapshot_worker(void *argument)
{
    struct frame_pacer_hud_metrics_cache *cache = argument;
    unsigned int iteration;

    for (iteration = 0; iteration < 1000; ++iteration) {
        struct frame_pacer_metrics_snapshot snapshot;

        frame_pacer_hud_metrics_cache_snapshot(cache, 100, &snapshot);
    }
    return 0;
}

int main(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;
    pthread_t threads[4];
    unsigned int index;

    frame_pacer_hud_metrics_cache_snapshot(0, 0, &snapshot);
    frame_pacer_hud_metrics_cache_snapshot(0, 0, 0);
    frame_pacer_hud_metrics_cache_init(0, 0, 0);
    frame_pacer_hud_metrics_cache_destroy(0);

    frame_pacer_hud_metrics_cache_init(&cache, 0, 0);
    assert(cache.initialized);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 100, &snapshot);
    assert(cache.sample_ns == 100);
    cache.snapshot.available = 0x80000000U;
    frame_pacer_hud_metrics_cache_snapshot(&cache, 101, &snapshot);
    assert(snapshot.available == 0x80000000U);
    assert(cache.sample_ns == 100);

    for (index = 0; index < 4; ++index)
        assert(!pthread_create(&threads[index], 0, snapshot_worker, &cache));
    for (index = 0; index < 4; ++index)
        assert(!pthread_join(threads[index], 0));

    /* A backwards clock observation refreshes rather than underflowing. */
    frame_pacer_hud_metrics_cache_snapshot(&cache, 99, &snapshot);
    assert(cache.sample_ns == 99);
    assert(snapshot.available != 0x80000000U);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    return 0;
}
