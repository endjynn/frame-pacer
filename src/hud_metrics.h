#ifndef FRAME_PACER_HUD_METRICS_H
#define FRAME_PACER_HUD_METRICS_H

#include "hud_drm_fdinfo.h"
#include "hud_nvml_provider.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

enum frame_pacer_metric {
    FRAME_PACER_METRIC_CPU_USE = 1U << 0,
    FRAME_PACER_METRIC_CPU_TEMP = 1U << 1,
    FRAME_PACER_METRIC_GPU_USE = 1U << 2,
    FRAME_PACER_METRIC_GPU_TEMP = 1U << 3,
    FRAME_PACER_METRIC_THREAD_CPU_USE = 1U << 5,
};

#define FRAME_PACER_THREAD_CPU_SLOTS_MAX 1024U

struct frame_pacer_thread_cpu_sample {
    uint32_t tid;
    uint64_t usage_usec;
    uint64_t sample_ns;
    bool started;
};

struct frame_pacer_metrics_snapshot {
    unsigned int available;
    unsigned int cpu_use_percent;
    unsigned int thread_cpu_percent;
    unsigned int cpu_temp_celsius;
    unsigned int gpu_use_percent;
    unsigned int gpu_temp_celsius;
};

struct frame_pacer_metrics {
    pthread_mutex_t mutex;
    bool initialized;
    uint64_t cpu_total;
    uint64_t cpu_idle;
    bool cpu_started;
    struct frame_pacer_thread_cpu_sample
        thread_cpu[FRAME_PACER_THREAD_CPU_SLOTS_MAX];
    uint64_t thread_cpu_sample_ns;
    unsigned int thread_cpu_percent;
    bool thread_cpu_available;
    char cpu_temp_path[256];
    /* The render node opened by this process is the cross-backend adapter
     * identity.  Providers are selected from it, never from user settings. */
    char gpu_render_node[32];
    char gpu_pci_bus_id[16];
    char gpu_temp_path[256];
    unsigned int gpu_vendor;
    unsigned int process_id;
    uint64_t nvml_retry_ns;
    unsigned int nvml_select_attempts;
    bool nvml_external;
    struct frame_pacer_drm_fdinfo drm_fdinfo;
    struct frame_pacer_nvml_provider nvml;
};

void frame_pacer_metrics_init(struct frame_pacer_metrics *,
                              const char *nvml_library,
                              unsigned int process_id);
void frame_pacer_metrics_destroy(struct frame_pacer_metrics *);
void frame_pacer_metrics_sample(struct frame_pacer_metrics *,
                                struct frame_pacer_metrics_snapshot *);
bool frame_pacer_metrics_parse_cpu(const char *line, uint64_t *total, uint64_t *idle);
bool frame_pacer_metrics_parse_cpu_stat(const char *line, const char *key,
                                        uint64_t *value);
bool frame_pacer_metrics_parse_render_node(const char *target, char *node, size_t node_size);

#ifdef FRAME_PACER_TEST
void frame_pacer_metrics_test_prune_thread_cpu_slots(
    struct frame_pacer_metrics *, uint64_t sample_ns);
bool frame_pacer_metrics_test_parse_temperature(const char *, unsigned int *);
bool frame_pacer_metrics_test_parse_gpu_vendor(const char *, unsigned int *);
bool frame_pacer_metrics_test_parse_pci_bus_id(const char *, char *, size_t);
void frame_pacer_metrics_test_set_gpu_identity(
    struct frame_pacer_metrics *, const char *render_node,
    unsigned int vendor, const char *pci_bus_id);
bool frame_pacer_metrics_test_find_amd_temperature(
    struct frame_pacer_metrics *, const char *drm_root);
#endif

#endif
