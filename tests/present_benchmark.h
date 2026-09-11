#ifndef FRAME_PACER_PRESENT_BENCHMARK_H
#define FRAME_PACER_PRESENT_BENCHMARK_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

/* Opt-in, backend-independent timing of completed presentation work. */
struct present_benchmark {
    unsigned int frames;
    unsigned int width;
    unsigned int height;
    unsigned int count;
    uint64_t wall_start;
    uint64_t cpu_start;
    uint64_t wall[4096];
    uint64_t cpu[4096];
};

static uint64_t benchmark_clock(clockid_t clock)
{
    struct timespec value;
    if (clock_gettime(clock, &value))
        abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static unsigned int benchmark_option(const char *name, unsigned int fallback)
{
    const char *text = getenv(name);
    char *end;
    unsigned long value;
    if (!text)
        return fallback;
    value = strtoul(text, &end, 10);
    if (!*text || *end || !value || value > 4096)
        abort();
    return (unsigned int)value;
}

static void benchmark_init(struct present_benchmark *benchmark,
                           unsigned int width, unsigned int height)
{
    *benchmark = (struct present_benchmark){
        .frames = benchmark_option("FRAME_PACER_BENCH_FRAMES", 2),
        .width = benchmark_option("FRAME_PACER_BENCH_WIDTH", width),
        .height = benchmark_option("FRAME_PACER_BENCH_HEIGHT", height),
    };
}

static void benchmark_begin(struct present_benchmark *benchmark)
{
    benchmark->wall_start = benchmark_clock(CLOCK_MONOTONIC);
    benchmark->cpu_start = benchmark_clock(CLOCK_PROCESS_CPUTIME_ID);
}

static int benchmark_resize_due(const struct present_benchmark *benchmark)
{
    return getenv("FRAME_PACER_BENCH_RESIZE") && benchmark->count &&
           benchmark->count < benchmark->frames && benchmark->count % 30 == 0;
}

static void benchmark_resize(struct present_benchmark *benchmark)
{
    benchmark->width = benchmark->width == 1440 ? 3840 : 1440;
    benchmark->height = benchmark->width == 1440 ? 900 : 2160;
    fprintf(stderr, "resize_frame=%u\n", benchmark->count);
}

static void benchmark_end(struct present_benchmark *benchmark)
{
    unsigned int index = benchmark->count++;
    benchmark->cpu[index] =
        benchmark_clock(CLOCK_PROCESS_CPUTIME_ID) - benchmark->cpu_start;
    benchmark->wall[index] =
        benchmark_clock(CLOCK_MONOTONIC) - benchmark->wall_start;
}

static void benchmark_report(const struct present_benchmark *benchmark)
{
    if (!getenv("FRAME_PACER_BENCH_FRAMES"))
        return;
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage))
        abort();
    fprintf(stderr, "peak_rss_kib=%ld\n", usage.ru_maxrss);
    puts("frame,wall_ns,cpu_ns");
    for (unsigned int index = 0; index < benchmark->count; ++index)
        printf("%u,%llu,%llu\n", index,
               (unsigned long long)benchmark->wall[index],
               (unsigned long long)benchmark->cpu[index]);
}

#endif
