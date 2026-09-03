#include "hud_metrics.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    unsigned int process_id = (unsigned int)getpid();
    if (argc == 2) {
        char *end;
        unsigned long value;
        errno = 0;
        value = strtoul(argv[1], &end, 10);
        if (errno || !argv[1][0] || *end || !value || value > UINT_MAX) {
            (void)fprintf(stderr, "usage: %s [graphics-process-id]\\n",
                          argv[0]);
            return 2;
        }
        process_id = (unsigned int)value;
    } else if (argc != 1) {
        (void)fprintf(stderr, "usage: %s [graphics-process-id]\\n", argv[0]);
        return 2;
    }
    frame_pacer_metrics_init(&metrics, 0, process_id);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    printf("available=0x%x cpu=%u cpu_temp=%u gpu=%u gpu_temp=%u\n",
           snapshot.available, snapshot.cpu_use_percent,
           snapshot.cpu_temp_celsius, snapshot.gpu_use_percent,
           snapshot.gpu_temp_celsius);
    frame_pacer_metrics_destroy(&metrics);
    return (snapshot.available &
            (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)) ==
                   (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)
               ? 0
               : 1;
}
