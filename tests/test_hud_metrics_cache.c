#include "hud_metrics_cache.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

struct sampler_context {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned int samples;
    unsigned int resets;
    pthread_t caller;
    pthread_t owner;
    bool block;
    bool release;
};

struct reader_context {
    struct frame_pacer_hud_metrics_cache *cache;
    uint64_t now_ns;
};

static void context_init(struct sampler_context *context)
{
    memset(context, 0, sizeof(*context));
    assert(!pthread_mutex_init(&context->mutex, 0));
    assert(!pthread_cond_init(&context->condition, 0));
    context->caller = pthread_self();
}

static void context_destroy(struct sampler_context *context)
{
    assert(!pthread_cond_destroy(&context->condition));
    assert(!pthread_mutex_destroy(&context->mutex));
}

static void test_sample(void *argument,
                        struct frame_pacer_metrics_snapshot *snapshot)
{
    struct sampler_context *context = argument;
    unsigned int generation;

    assert(!pthread_mutex_lock(&context->mutex));
    assert(!pthread_equal(context->caller, pthread_self()));
    if (!context->samples)
        context->owner = pthread_self();
    assert(pthread_equal(context->owner, pthread_self()));
    generation = ++context->samples;
    assert(!pthread_cond_broadcast(&context->condition));
    while (context->block && !context->release)
        assert(!pthread_cond_wait(&context->condition, &context->mutex));
    assert(!pthread_mutex_unlock(&context->mutex));
    snapshot->available =
        FRAME_PACER_METRIC_CPU_USE | FRAME_PACER_METRIC_GPU_USE;
    snapshot->cpu_use_percent = generation;
    snapshot->gpu_use_percent = generation;
}

static void test_reset(void *argument)
{
    struct sampler_context *context = argument;

    assert(!pthread_mutex_lock(&context->mutex));
    assert(context->samples);
    assert(pthread_equal(context->owner, pthread_self()));
    ++context->resets;
    assert(!pthread_mutex_unlock(&context->mutex));
}

static void wait_for_samples(struct sampler_context *context,
                             unsigned int samples)
{
    assert(!pthread_mutex_lock(&context->mutex));
    while (context->samples < samples)
        assert(!pthread_cond_wait(&context->condition, &context->mutex));
    assert(!pthread_mutex_unlock(&context->mutex));
}

static unsigned int reset_count(struct sampler_context *context)
{
    unsigned int resets;

    assert(!pthread_mutex_lock(&context->mutex));
    resets = context->resets;
    assert(!pthread_mutex_unlock(&context->mutex));
    return resets;
}

static void release_sampler(struct sampler_context *context)
{
    assert(!pthread_mutex_lock(&context->mutex));
    context->release = true;
    assert(!pthread_cond_broadcast(&context->condition));
    assert(!pthread_mutex_unlock(&context->mutex));
}

static void wait_until_idle(struct frame_pacer_hud_metrics_cache *cache)
{
    assert(!pthread_mutex_lock(&cache->mutex));
    while (cache->sample_in_progress || cache->request_pending)
        assert(!pthread_cond_wait(&cache->condition, &cache->mutex));
    assert(!pthread_mutex_unlock(&cache->mutex));
}

static void *snapshot_reader(void *argument)
{
    struct reader_context *context = argument;
    unsigned int iteration;

    for (iteration = 0; iteration < 1000; ++iteration) {
        struct frame_pacer_metrics_snapshot snapshot;

        frame_pacer_hud_metrics_cache_snapshot(context->cache, context->now_ns,
                                               &snapshot);
        assert(snapshot.cpu_use_percent == snapshot.gpu_use_percent);
    }
    return 0;
}

static void *destroy_cache(void *argument)
{
    frame_pacer_hud_metrics_cache_destroy(argument);
    return 0;
}

static void null_and_unused_cache_are_safe(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;

    frame_pacer_hud_metrics_cache_snapshot(0, 0, &snapshot);
    frame_pacer_hud_metrics_cache_snapshot(0, 0, 0);
    frame_pacer_hud_metrics_cache_init(0, 0);
    frame_pacer_hud_metrics_cache_destroy(0);
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    assert(cache.initialized && !cache.worker_started &&
           !cache.metrics_initialized);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    frame_pacer_hud_metrics_cache_destroy(&cache);
}

static void cadence_and_reset_are_worker_driven(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;
    struct sampler_context context;

    context_init(&context);
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_test_set_sampler(&cache, test_sample,
                                                   test_reset, &context);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    assert(!snapshot.available);
    wait_for_samples(&context, 1);
    wait_until_idle(&cache);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    assert(snapshot.cpu_use_percent == 1 && snapshot.gpu_use_percent == 1);
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS - 1, &snapshot);
    wait_until_idle(&cache);
    assert(context.samples == 1);

    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS, &snapshot);
    wait_for_samples(&context, 2);
    wait_until_idle(&cache);
    assert(reset_count(&context) == 0);

    frame_pacer_hud_metrics_cache_snapshot(
        &cache,
        FRAME_PACER_HUD_METRICS_SAMPLE_NS + FRAME_PACER_HUD_METRICS_RESET_NS -
            1,
        &snapshot);
    wait_for_samples(&context, 3);
    wait_until_idle(&cache);
    assert(reset_count(&context) == 0);

    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS * 2, &snapshot);
    wait_for_samples(&context, 4);
    wait_until_idle(&cache);
    assert(reset_count(&context) == 1);

    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS * 8, &snapshot);
    wait_for_samples(&context, 5);
    wait_until_idle(&cache);
    assert(reset_count(&context) == 2);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    context_destroy(&context);
}

static void concurrent_requests_coalesce(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;
    struct sampler_context context;
    struct reader_context reader = {.cache = &cache, .now_ns = 0};
    pthread_t readers[4];
    unsigned int index;

    context_init(&context);
    context.block = true;
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_test_set_sampler(&cache, test_sample,
                                                   test_reset, &context);
    for (index = 0; index < 4; ++index)
        assert(!pthread_create(&readers[index], 0, snapshot_reader, &reader));
    for (index = 0; index < 4; ++index)
        assert(!pthread_join(readers[index], 0));
    wait_for_samples(&context, 1);
    assert(cache.worker_started);

    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS, &snapshot);
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS * 2, &snapshot);
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS * 3, &snapshot);
    assert(!snapshot.available);
    /* A clock rewind queues a reset without touching the active sampler. */
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    assert(reset_count(&context) == 0);
    release_sampler(&context);
    wait_for_samples(&context, 2);
    wait_until_idle(&cache);
    assert(context.samples == 2);
    assert(reset_count(&context) == 1);

    reader.now_ns = 0;
    for (index = 0; index < 4; ++index)
        assert(!pthread_create(&readers[index], 0, snapshot_reader, &reader));
    for (index = 0; index < 4; ++index)
        assert(!pthread_join(readers[index], 0));
    assert(context.samples == 2);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    context_destroy(&context);
}

static void thread_failure_is_not_retried(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;

    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_test_fail_thread_creation(&cache);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    assert(cache.worker_failed && !cache.worker_started && !snapshot.available);
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS, &snapshot);
    assert(cache.worker_failed && !cache.worker_started && !snapshot.available);
    frame_pacer_hud_metrics_cache_destroy(&cache);
}

static void destruction_waits_for_sampling(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;
    struct sampler_context context;
    pthread_t destructor;

    context_init(&context);
    context.block = true;
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_test_set_sampler(&cache, test_sample,
                                                   test_reset, &context);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    wait_for_samples(&context, 1);
    assert(!pthread_create(&destructor, 0, destroy_cache, &cache));
    release_sampler(&context);
    assert(!pthread_join(destructor, 0));
    context_destroy(&context);
}

static void real_sampler_has_worker_owned_lifetime(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;

    /* Exercise the real sampler, not just callbacks, under the sanitizers. */
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    wait_until_idle(&cache);
    assert(cache.metrics_initialized);
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_RESET_NS, &snapshot);
    wait_until_idle(&cache);
    assert(cache.metrics.initialized);
    frame_pacer_hud_metrics_cache_destroy(&cache);
    assert(!cache.metrics.initialized);
}

int main(void)
{
    null_and_unused_cache_are_safe();
    cadence_and_reset_are_worker_driven();
    concurrent_requests_coalesce();
    thread_failure_is_not_retried();
    destruction_waits_for_sampling();
    real_sampler_has_worker_owned_lifetime();
    return 0;
}
