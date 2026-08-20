#include <string.h>
#include <stdint.h>
#include <unistd.h>

struct nvml_utilization { unsigned int gpu, memory; };

int nvmlInit_v2(void) { return 0; }
int nvmlShutdown(void) { return 0; }
struct nvml_process_info { unsigned int pid; unsigned long long used_gpu_memory; };
static unsigned int process_queries;

int nvmlDeviceGetCount_v2(unsigned int *count)
{
    if (!count) return 1;
    *count = 1;
    return 0;
}
int nvmlDeviceGetHandleByIndex_v2(unsigned int index, void **device)
{
    if (index || !device) return 1;
    *device = (void *)(uintptr_t)0x1234;
    return 0;
}
int nvmlDeviceGetGraphicsRunningProcesses(void *device, unsigned int *count, struct nvml_process_info *processes)
{
    if (device != (void *)(uintptr_t)0x1234 || !count) return 1;
    if (!processes) {
        *count = 1;
        return 1;
    }
    if (*count < 1) return 1;
    processes[0].pid = ++process_queries == 1 ? 1U : (unsigned int)getpid();
    processes[0].used_gpu_memory = 0;
    *count = 1;
    return 0;
}
int nvmlDeviceGetUtilizationRates(void *device, struct nvml_utilization *utilization)
{
    if (device != (void *)(uintptr_t)0x1234 || !utilization) return 1;
    utilization->gpu = 37;
    utilization->memory = 12;
    return 0;
}
int nvmlDeviceGetTemperature(void *device, unsigned int sensor, unsigned int *temperature)
{
    if (device != (void *)(uintptr_t)0x1234 || sensor || !temperature) return 1;
    *temperature = 64;
    return 0;
}
