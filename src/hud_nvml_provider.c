#define _POSIX_C_SOURCE 200809L
#include "hud_nvml_provider.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NVML_MAX_DEVICES 64U
#define NVML_MAX_GRAPHICS_PROCESSES 4096U

struct nvml_utilization {
    unsigned int gpu;
    unsigned int memory;
};

struct nvml_process_info {
    unsigned int pid;
    unsigned long long used_gpu_memory;
};

static void function_from_symbol(void *symbol, void *function, size_t size)
{
    memcpy(function, &symbol, size);
}

bool frame_pacer_nvml_provider_init(struct frame_pacer_nvml_provider *provider,
                                    const char *library)
{
    void *symbol;
    int (*init)(void) = 0;

    if (!provider)
        return false;
    memset(provider, 0, sizeof(*provider));
    provider->library =
        dlopen(library && *library ? library : "libnvidia-ml.so.1",
               RTLD_LAZY | RTLD_LOCAL);
    if (!provider->library)
        return false;

    symbol = dlsym(provider->library, "nvmlInit_v2");
    function_from_symbol(symbol, &init, sizeof(init));
    symbol = dlsym(provider->library, "nvmlShutdown");
    function_from_symbol(symbol, &provider->shutdown,
                         sizeof(provider->shutdown));
    symbol = dlsym(provider->library, "nvmlDeviceGetCount_v2");
    function_from_symbol(symbol, &provider->get_count,
                         sizeof(provider->get_count));
    symbol = dlsym(provider->library, "nvmlDeviceGetHandleByIndex_v2");
    function_from_symbol(symbol, &provider->get_device,
                         sizeof(provider->get_device));
    symbol = dlsym(provider->library, "nvmlDeviceGetHandleByPciBusId_v2");
    function_from_symbol(symbol, &provider->get_device_by_pci,
                         sizeof(provider->get_device_by_pci));
    symbol = dlsym(provider->library, "nvmlDeviceGetGraphicsRunningProcesses");
    function_from_symbol(symbol, &provider->get_graphics_processes,
                         sizeof(provider->get_graphics_processes));
    symbol = dlsym(provider->library, "nvmlDeviceGetUtilizationRates");
    function_from_symbol(symbol, &provider->utilization,
                         sizeof(provider->utilization));
    symbol = dlsym(provider->library, "nvmlDeviceGetTemperature");
    function_from_symbol(symbol, &provider->temperature,
                         sizeof(provider->temperature));

    if (!init || !provider->shutdown || !provider->utilization ||
        !provider->temperature || init() != 0) {
        (void)dlclose(provider->library);
        memset(provider, 0, sizeof(*provider));
        return false;
    }
    provider->started = true;
    return true;
}

void frame_pacer_nvml_provider_destroy(
    struct frame_pacer_nvml_provider *provider)
{
    if (!provider)
        return;
    if (provider->started)
        (void)provider->shutdown();
    if (provider->library)
        (void)dlclose(provider->library);
    memset(provider, 0, sizeof(*provider));
}

bool frame_pacer_nvml_provider_select_process(
    struct frame_pacer_nvml_provider *provider, unsigned int process_id)
{
    unsigned int count, index;

    if (!provider || !provider->started || !provider->get_count ||
        !provider->get_device || !provider->get_graphics_processes ||
        !process_id || provider->get_count(&count) != 0 ||
        count > NVML_MAX_DEVICES)
        return false;
    provider->device = 0;
    for (index = 0; index < count; ++index) {
        unsigned int process_count = 0, process_index, capacity;
        struct nvml_process_info *processes;
        void *device = 0;

        if (provider->get_device(index, &device) != 0 || !device)
            continue;
        (void)provider->get_graphics_processes(device, &process_count, 0);
        if (!process_count || process_count > NVML_MAX_GRAPHICS_PROCESSES)
            continue;
        processes = calloc(process_count, sizeof(*processes));
        if (!processes)
            continue;
        capacity = process_count;
        if (provider->get_graphics_processes(device, &process_count,
                                             processes) == 0) {
            if (process_count > capacity)
                process_count = capacity;
            for (process_index = 0; process_index < process_count;
                 ++process_index) {
                if (processes[process_index].pid == process_id) {
                    provider->device = device;
                    break;
                }
            }
        }
        free(processes);
        if (provider->device)
            return true;
    }
    return false;
}

bool frame_pacer_nvml_provider_select_pci(
    struct frame_pacer_nvml_provider *provider, const char *pci_bus_id)
{
    void *device = 0;

    if (!provider || !provider->started || !provider->get_device_by_pci ||
        !pci_bus_id || !*pci_bus_id ||
        provider->get_device_by_pci(pci_bus_id, &device) != 0 || !device)
        return false;
    provider->device = device;
    return true;
}

bool frame_pacer_nvml_provider_sample(
    struct frame_pacer_nvml_provider *provider,
    struct frame_pacer_nvml_sample *sample)
{
    struct nvml_utilization utilization;

    if (!sample)
        return false;
    memset(sample, 0, sizeof(*sample));
    if (!provider || !provider->started || !provider->device)
        return false;
    if (provider->utilization(provider->device, &utilization) == 0 &&
        utilization.gpu <= 100) {
        sample->gpu_use_percent = utilization.gpu;
        sample->available |= FRAME_PACER_NVML_GPU_USE;
    }
    if (provider->temperature(provider->device, 0, &sample->gpu_temp_celsius) ==
            0 &&
        sample->gpu_temp_celsius <= 200)
        sample->available |= FRAME_PACER_NVML_GPU_TEMP;
    return sample->available != 0;
}
