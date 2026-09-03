#define _GNU_SOURCE
#include "hud_metrics_cache.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ITERATIONS UINT64_C(20000000)

struct sampler_context {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned int samples;
    bool block;
    bool release;
};

static volatile uint64_t sink;

static uint64_t timestamp_ns(void)
{
    struct timespec value;

    assert(!clock_gettime(CLOCK_MONOTONIC_RAW, &value));
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static void pin_current_cpu(void)
{
    cpu_set_t set;
    int cpu = sched_getcpu();

    if (cpu < 0)
        return;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

static void sample(void *argument,
                   struct frame_pacer_metrics_snapshot *snapshot)
{
    struct sampler_context *context = argument;

    assert(!pthread_mutex_lock(&context->mutex));
    ++context->samples;
    assert(!pthread_cond_broadcast(&context->condition));
    while (context->block && !context->release)
        assert(!pthread_cond_wait(&context->condition, &context->mutex));
    assert(!pthread_mutex_unlock(&context->mutex));
    snapshot->available = FRAME_PACER_METRIC_CPU_USE;
    snapshot->cpu_use_percent = 50;
}

static void wait_for_sample(struct sampler_context *context, unsigned int count)
{
    assert(!pthread_mutex_lock(&context->mutex));
    while (context->samples < count)
        assert(!pthread_cond_wait(&context->condition, &context->mutex));
    assert(!pthread_mutex_unlock(&context->mutex));
}

static double benchmark_snapshots(struct frame_pacer_hud_metrics_cache *cache,
                                  uint64_t now_ns)
{
    struct frame_pacer_metrics_snapshot snapshot;
    uint64_t begin, index, total = 0;

    begin = timestamp_ns();
    for (index = 0; index < ITERATIONS; ++index) {
        frame_pacer_hud_metrics_cache_snapshot(cache, now_ns, &snapshot);
        total += snapshot.cpu_use_percent;
    }
    sink = total;
    return (double)(timestamp_ns() - begin) / (double)ITERATIONS;
}

int main(void)
{
    struct frame_pacer_hud_metrics_cache cache;
    struct frame_pacer_metrics_snapshot snapshot;
    struct sampler_context context;

    memset(&context, 0, sizeof(context));
    assert(!pthread_mutex_init(&context.mutex, 0));
    assert(!pthread_cond_init(&context.condition, 0));
    pin_current_cpu();
    frame_pacer_hud_metrics_cache_init(&cache, 0);
    frame_pacer_hud_metrics_cache_test_set_sampler(&cache, sample, 0, &context);
    frame_pacer_hud_metrics_cache_snapshot(&cache, 0, &snapshot);
    wait_for_sample(&context, 1);

    (void)benchmark_snapshots(&cache, 0);
    printf("metrics_cache_idle_ns %.3f\n", benchmark_snapshots(&cache, 0));

    assert(!pthread_mutex_lock(&context.mutex));
    context.block = true;
    assert(!pthread_mutex_unlock(&context.mutex));
    frame_pacer_hud_metrics_cache_snapshot(
        &cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS, &snapshot);
    wait_for_sample(&context, 2);
    printf("metrics_cache_sampling_ns %.3f\n",
           benchmark_snapshots(&cache, FRAME_PACER_HUD_METRICS_SAMPLE_NS));

    assert(!pthread_mutex_lock(&context.mutex));
    context.release = true;
    assert(!pthread_cond_broadcast(&context.condition));
    assert(!pthread_mutex_unlock(&context.mutex));
    frame_pacer_hud_metrics_cache_destroy(&cache);
    assert(!pthread_cond_destroy(&context.condition));
    assert(!pthread_mutex_destroy(&context.mutex));
    return sink == UINT64_MAX;
}
