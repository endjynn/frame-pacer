#define _POSIX_C_SOURCE 200809L
#include "hud_metrics.h"
#include "hud_nvml_client.h"

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

static void wait_briefly(void)
{
    struct timespec remaining = { .tv_nsec = 10000000L };

    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
}

int main(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    unsigned int iteration;

    frame_pacer_metrics_init(&metrics, "/frame-pacer/missing-i386-nvml.so",
                             (unsigned int)getpid());
    frame_pacer_metrics_test_set_gpu_identity(
        &metrics, "renderD128", 0x8086U, "0000:00:02.0");
    frame_pacer_metrics_sample(&metrics, &snapshot);
    wait_briefly();
    assert(frame_pacer_nvml_client_test_attempts() == 0);

    frame_pacer_metrics_test_set_gpu_identity(
        &metrics, "renderD129", 0x10deU, "0000:01:00.0");
    for (iteration = 0; iteration < 300; ++iteration) {
        frame_pacer_metrics_sample(&metrics, &snapshot);
        if ((snapshot.available &
             (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)) ==
            (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP))
            break;
        wait_briefly();
    }
    assert(iteration < 300);
    assert(snapshot.gpu_use_percent == 37);
    assert(snapshot.gpu_temp_celsius == 64);
    assert(frame_pacer_nvml_client_test_attempts() == 1);
    frame_pacer_metrics_destroy(&metrics);
    assert(frame_pacer_nvml_client_test_child() < 0);
}
