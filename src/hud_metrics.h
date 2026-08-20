#ifndef FRAME_PACER_HUD_METRICS_H
#define FRAME_PACER_HUD_METRICS_H

#include "hud_drm_fdinfo.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

enum frame_pacer_metric {
    FRAME_PACER_METRIC_CPU_USE = 1U << 0,
    FRAME_PACER_METRIC_CPU_TEMP = 1U << 1,
    FRAME_PACER_METRIC_GPU_USE = 1U << 2,
    FRAME_PACER_METRIC_GPU_TEMP = 1U << 3,
    FRAME_PACER_METRIC_CPU_PEAK = 1U << 4,
    FRAME_PACER_METRIC_THREAD_CPU_USE = 1U << 5,
};

#define FRAME_PACER_CPU_MAX_CORES 1024U

struct frame_pacer_cpu_sample {
    uint64_t total;
    uint64_t idle;
    bool started;
};

struct frame_pacer_thread_cpu_sample {
    uint32_t tid;
    uint64_t usage_usec;
    uint64_t sample_ns;
    bool started;
};

struct frame_pacer_metrics_snapshot {
    unsigned int available;
    unsigned int cpu_use_percent;
    unsigned int cpu_peak_percent;
    unsigned int thread_cpu_percent;
    unsigned int cpu_temp_celsius;
    unsigned int gpu_use_percent;
    unsigned int gpu_temp_celsius;
};

struct frame_pacer_metrics {
    pthread_mutex_t mutex;
    uint64_t cpu_total;
    uint64_t cpu_idle;
    bool cpu_started;
    struct frame_pacer_cpu_sample cpu_cores[FRAME_PACER_CPU_MAX_CORES];
    struct frame_pacer_thread_cpu_sample thread_cpu[FRAME_PACER_CPU_MAX_CORES];
    uint64_t thread_cpu_sample_ns;
    unsigned int thread_cpu_percent;
    bool thread_cpu_available;
    char cpu_temp_path[256];
    /* The render node opened by this process is the cross-backend adapter
     * identity.  Providers are selected from it, never from user settings. */
    char gpu_render_node[32];
    unsigned int process_id;
    uint64_t nvml_retry_ns;
    struct frame_pacer_drm_fdinfo drm_fdinfo;
    void *nvml_library;
    void *nvml_device;
    bool nvml_started;
    int (*nvml_shutdown)(void);
    int (*nvml_get_count)(unsigned int *);
    int (*nvml_get_device)(unsigned int, void **);
    int (*nvml_get_graphics_processes)(void *, unsigned int *, void *);
    int (*nvml_utilization)(void *, void *);
    int (*nvml_temperature)(void *, unsigned int, unsigned int *);
};

void frame_pacer_metrics_init(struct frame_pacer_metrics *,
                              const char *nvml_library,
                              unsigned int process_id);
void frame_pacer_metrics_destroy(struct frame_pacer_metrics *);
/* Re-evaluate the NVIDIA association.  A process may become visible to NVML
 * only after graphics initialisation; no association means GPU fields stay N/A. */
void frame_pacer_metrics_select_gpu(struct frame_pacer_metrics *, unsigned int process_id);
void frame_pacer_metrics_sample(struct frame_pacer_metrics *,
                                struct frame_pacer_metrics_snapshot *);
bool frame_pacer_metrics_parse_cpu(const char *line, uint64_t *total, uint64_t *idle);
bool frame_pacer_metrics_parse_cpu_stat(const char *line, const char *key,
                                        uint64_t *value);
bool frame_pacer_metrics_parse_render_node(const char *target, char *node, size_t node_size);

#endif
