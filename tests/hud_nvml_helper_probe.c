#define _POSIX_C_SOURCE 200809L
#include "hud_nvml_client.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ns(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static void wait_briefly(void)
{
    struct timespec remaining = { .tv_nsec = 10000000L };

    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
}

int main(int argc, char **argv)
{
    struct frame_pacer_nvml_message first, next;
    uint64_t first_ns, elapsed;
    unsigned int iteration;
    int result = 1;

    if (argc != 2 || strlen(argv[1]) != 12) return 2;
    if (!frame_pacer_nvml_client_acquire((unsigned int)getpid(), argv[1]))
        return 3;
    for (iteration = 0; iteration < 500; ++iteration) {
        if (frame_pacer_nvml_client_snapshot(&first) &&
            first.sample.available ==
                (FRAME_PACER_NVML_GPU_USE | FRAME_PACER_NVML_GPU_TEMP))
            break;
        wait_briefly();
    }
    if (iteration == 500) goto done;
    first_ns = monotonic_ns();
    for (iteration = 0; iteration < 300; ++iteration) {
        if (frame_pacer_nvml_client_snapshot(&next) &&
            next.sequence != first.sequence &&
            next.sample.available ==
                (FRAME_PACER_NVML_GPU_USE | FRAME_PACER_NVML_GPU_TEMP))
            break;
        wait_briefly();
    }
    elapsed = monotonic_ns() - first_ns;
    if (iteration == 300 || elapsed < UINT64_C(700000000) ||
        elapsed > UINT64_C(2500000000) || next.sample.gpu_use_percent > 100 ||
        next.sample.gpu_temp_celsius > 200)
        goto done;
    printf("NVML helper PCI %s: GPU %u%% %u C, cadence %.3f s\n", argv[1],
           next.sample.gpu_use_percent, next.sample.gpu_temp_celsius,
           (double)elapsed / 1000000000.0);
    result = 0;
done:
    frame_pacer_nvml_client_release();
    return result;
}
