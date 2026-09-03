#ifndef FRAME_PACER_HUD_NVML_PROVIDER_H
#define FRAME_PACER_HUD_NVML_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_NVML_INTERNAL __attribute__((visibility("hidden")))

enum frame_pacer_nvml_metric {
    FRAME_PACER_NVML_GPU_USE = 1U << 0,
    FRAME_PACER_NVML_GPU_TEMP = 1U << 1,
};

struct frame_pacer_nvml_sample {
    unsigned int available;
    unsigned int gpu_use_percent;
    unsigned int gpu_temp_celsius;
};

struct frame_pacer_nvml_provider {
    void *library;
    void *device;
    bool started;
    int (*shutdown)(void);
    int (*get_count)(unsigned int *);
    int (*get_device)(unsigned int, void **);
    int (*get_device_by_pci)(const char *, void **);
    int (*get_graphics_processes)(void *, unsigned int *, void *);
    int (*utilization)(void *, void *);
    int (*temperature)(void *, unsigned int, unsigned int *);
};

FRAME_PACER_NVML_INTERNAL bool
frame_pacer_nvml_provider_init(struct frame_pacer_nvml_provider *,
                               const char *library);
FRAME_PACER_NVML_INTERNAL void
frame_pacer_nvml_provider_destroy(struct frame_pacer_nvml_provider *);
FRAME_PACER_NVML_INTERNAL
bool frame_pacer_nvml_provider_select_process(
    struct frame_pacer_nvml_provider *, unsigned int process_id);
FRAME_PACER_NVML_INTERNAL
bool frame_pacer_nvml_provider_select_pci(struct frame_pacer_nvml_provider *,
                                          const char *pci_bus_id);
FRAME_PACER_NVML_INTERNAL
bool frame_pacer_nvml_provider_sample(struct frame_pacer_nvml_provider *,
                                      struct frame_pacer_nvml_sample *);

#endif
