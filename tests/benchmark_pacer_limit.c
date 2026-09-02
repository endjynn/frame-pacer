#define _GNU_SOURCE
#include "log_retention.h"
#include "pacer_limit.h"

#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FAST_ITERATIONS UINT64_C(20000000)
#define RELOAD_ITERATIONS UINT64_C(20000)

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

    if (cpu < 0) return;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

#ifdef FRAME_PACER_BENCHMARK_BASELINE
static void disabled_log(struct frame_pacer_runtime_log *log,
                         const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    frame_pacer_runtime_log_vwrite(log, format, arguments);
    va_end(arguments);
}
#endif

static uint32_t poll_limit(struct frame_pacer_limit *limit, uint64_t now)
{
#ifdef FRAME_PACER_BENCHMARK_BASELINE
    bool changed;

    return frame_pacer_limit_poll(limit, now, &changed);
#else
    return frame_pacer_limit_poll(limit, now);
#endif
}

static void write_config(int fd)
{
    static const char config[] =
        "global_fps_limit = 60\n"
        "hud = off\n";
    size_t offset = 0;

    while (offset < sizeof(config) - 1) {
        ssize_t written = write(fd, config + offset, sizeof(config) - 1 - offset);

        if (written < 0 && errno == EINTR) continue;
        assert(written > 0);
        offset += (size_t)written;
    }
    assert(!fsync(fd));
}

static double benchmark_poll(struct frame_pacer_limit *limit, uint64_t iterations)
{
    uint64_t begin, index, total = 0;

    begin = timestamp_ns();
    for (index = 0; index < iterations; ++index)
        total += poll_limit(limit, UINT64_C(2));
    sink = total;
    return (double)(timestamp_ns() - begin) / (double)iterations;
}

static double benchmark_options(struct frame_pacer_limit *limit,
                                uint64_t iterations)
{
    uint64_t begin, index, total = 0;

    begin = timestamp_ns();
    for (index = 0; index < iterations; ++index) {
        bool enabled;

        total += frame_pacer_limit_hud_enabled(limit);
        total += frame_pacer_limit_thread_cpu_quota(limit, &enabled);
        total += enabled;
    }
    sink = total;
    return (double)(timestamp_ns() - begin) / (double)iterations;
}

static double benchmark_disabled_logging(uint64_t iterations)
{
    struct frame_pacer_runtime_log log = FRAME_PACER_RUNTIME_LOG_INITIALIZER(512);
    uint64_t begin, index, total = 0;

    begin = timestamp_ns();
    for (index = 0; index < iterations; ++index) {
#ifdef FRAME_PACER_BENCHMARK_BASELINE
        disabled_log(&log, "frame-pacer: present cap=%u\n", 75U);
#else
        total += frame_pacer_runtime_log_active(&log);
#endif
    }
    sink = total;
    return (double)(timestamp_ns() - begin) / (double)iterations;
}

static double benchmark_reload(struct frame_pacer_limit *limit,
                               uint64_t iterations)
{
    uint64_t begin, index, now = UINT64_C(2000000000), total = 0;

    begin = timestamp_ns();
    for (index = 0; index < iterations; ++index) {
        total += poll_limit(limit, now);
        now += FRAME_PACER_CONFIG_POLL_NS;
    }
    sink = total;
    return (double)(timestamp_ns() - begin) / (double)iterations;
}

int main(void)
{
    struct frame_pacer_limit limit;
    char path[] = "/tmp/frame-pacer-benchmark.XXXXXX";
    int fd;

    pin_current_cpu();
    fd = mkstemp(path);
    assert(fd >= 0);
    assert(!fchmod(fd, 0600));
    write_config(fd);
    assert(!close(fd));
    frame_pacer_limit_init(&limit);
    assert(snprintf(limit.path, sizeof(limit.path), "%s", path) > 0);
    assert(poll_limit(&limit, UINT64_C(1)) == 60);

    (void)benchmark_poll(&limit, UINT64_C(100000));
    (void)benchmark_options(&limit, UINT64_C(100000));
    (void)benchmark_disabled_logging(UINT64_C(100000));
    printf("poll_ns %.3f\n", benchmark_poll(&limit, FAST_ITERATIONS));
    printf("options_ns %.3f\n", benchmark_options(&limit, FAST_ITERATIONS));
    printf("disabled_log_ns %.3f\n",
           benchmark_disabled_logging(FAST_ITERATIONS));
    printf("reload_ns %.3f\n", benchmark_reload(&limit, RELOAD_ITERATIONS));

    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    return sink == UINT64_MAX;
}
