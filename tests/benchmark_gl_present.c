#define _GNU_SOURCE
#include <GL/glx.h>

#include <assert.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITERATIONS UINT64_C(5000000)

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

int main(void)
{
    uint64_t begin, index;

    pin_current_cpu();
    for (index = 0; index < 10000; ++index)
        glXSwapBuffers((Display *)1, 0);
    begin = timestamp_ns();
    for (index = 0; index < ITERATIONS; ++index)
        glXSwapBuffers((Display *)1, 0);
    printf("glx_present_ns %.3f\n",
           (double)(timestamp_ns() - begin) / (double)ITERATIONS);
    return 0;
}
