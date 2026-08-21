#define _POSIX_C_SOURCE 200809L
#include "hud_metrics.h"
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct nvml_utilization {
    unsigned int gpu;
    unsigned int memory;
};

struct nvml_process_info {
    unsigned int pid;
    unsigned long long used_gpu_memory;
};

#define NVML_MAX_GRAPHICS_PROCESSES 4096U
#define NVML_RETRY_INTERVAL_NS UINT64_C(1000000000)
#define THREAD_CPU_SAMPLE_INTERVAL_NS UINT64_C(1000000000)

static void function_from_symbol(void *symbol, void *function, size_t size)
{
    memcpy(function, &symbol, size);
}

static bool parse_uint64(const char **text, uint64_t *value)
{
    const char *cursor = *text;
    uint64_t parsed = 0;

    if (!isdigit((unsigned char)*cursor)) return false;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');

        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++cursor;
    } while (isdigit((unsigned char)*cursor));
    *text = cursor;
    *value = parsed;
    return true;
}

static bool parse_temperature(const char *text, unsigned int *celsius)
{
    const char *cursor = text;
    uint64_t millidegrees;

    if (!text || !celsius || !parse_uint64(&cursor, &millidegrees))
        return false;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor || millidegrees > UINT64_C(200000)) return false;
    *celsius = (unsigned int)((millidegrees + 500U) / 1000U);
    return true;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_metrics_test_parse_temperature(const char *text,
                                                 unsigned int *celsius)
{
    return parse_temperature(text, celsius);
}
#endif

bool frame_pacer_metrics_parse_cpu(const char *line, uint64_t *total, uint64_t *idle)
{
    uint64_t values[8];
    uint64_t parsed_total = 0;
    uint64_t parsed_idle;
    size_t index;
    const char *cursor;

    if (!line || !total || !idle || strncmp(line, "cpu", 3)) return false;
    cursor = line + 3;
    while (isdigit((unsigned char)*cursor)) ++cursor;
    if (*cursor != ' ' && *cursor != '\t') return false;
    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!parse_uint64(&cursor, &values[index])) return false;
        if (*cursor && *cursor != ' ' && *cursor != '\t' &&
            *cursor != '\r' && *cursor != '\n')
            return false;
    }
    /* Newer kernels may append counters after the eight fields used by the
     * utilization formula. Validate rather than interpret those fields. */
    for (;;) {
        uint64_t ignored;

        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
        if (!*cursor) break;
        if (!parse_uint64(&cursor, &ignored)) return false;
        if (*cursor && *cursor != ' ' && *cursor != '\t' &&
            *cursor != '\r' && *cursor != '\n')
            return false;
    }
    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (UINT64_MAX - parsed_total < values[index])
            return false;
        parsed_total += values[index];
    }
    if (UINT64_MAX - values[3] < values[4])
        return false;
    parsed_idle = values[3] + values[4];
    *total = parsed_total;
    *idle = parsed_idle;
    return true;
}

bool frame_pacer_metrics_parse_cpu_stat(const char *line, const char *key,
                                        uint64_t *value)
{
    const char *cursor;
    uint64_t parsed;
    size_t key_length;

    if (!line || !key || !*key || !value)
        return false;
    key_length = strlen(key);
    if (strncmp(line, key, key_length) ||
        (line[key_length] != ' ' && line[key_length] != '\t'))
        return false;
    cursor = line + key_length;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!parse_uint64(&cursor, &parsed)) return false;
    while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor) return false;
    *value = parsed;
    return true;
}

static bool thread_cgroup_root(char *output, size_t size)
{
    char line[2048]; FILE *file = fopen("/proc/self/cgroup", "re");
    const char *marker = "/frame-pacer-thread-cpu/t-";
    char *match; int written;

    if (!file || !fgets(line, sizeof(line), file) ||
        (!strchr(line, '\n') && !feof(file))) {
        if (file) (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    if (strncmp(line, "0::/", 4) || !(match = strstr(line + 3, marker)))
        return false;
    written = snprintf(output, size, "/sys/fs/cgroup%.*s/frame-pacer-thread-cpu",
                       (int)(match - (line + 3)), line + 3);
    return written > 0 && (size_t)written < size;
}

static bool child_usage_usec(const char *root, const char *name,
                             uint64_t *usage_usec)
{
    char path[PATH_MAX], line[256]; FILE *file;

    int written = snprintf(path, sizeof(path), "%s/%s/cpu.stat", root, name);

    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(file = fopen(path, "re")))
        return false;
    while (fgets(line, sizeof(line), file))
        if (frame_pacer_metrics_parse_cpu_stat(line, "usage_usec", usage_usec)) {
            (void)fclose(file);
            return true;
        }
    (void)fclose(file);
    return false;
}

static struct frame_pacer_thread_cpu_sample *thread_cpu_slot(
    struct frame_pacer_metrics *metrics, uint32_t tid)
{
    unsigned int i;
    for (i = 0; i < FRAME_PACER_THREAD_CPU_SLOTS_MAX; ++i)
        if (metrics->thread_cpu[i].started && metrics->thread_cpu[i].tid == tid)
            return &metrics->thread_cpu[i];
    for (i = 0; i < FRAME_PACER_THREAD_CPU_SLOTS_MAX; ++i)
        if (!metrics->thread_cpu[i].started) return &metrics->thread_cpu[i];
    return 0;
}

static void invalidate_thread_cpu(struct frame_pacer_metrics *metrics)
{
    memset(metrics->thread_cpu, 0, sizeof(metrics->thread_cpu));
    metrics->thread_cpu_sample_ns = 0;
    metrics->thread_cpu_percent = 0;
    metrics->thread_cpu_available = false;
}

static void prune_thread_cpu_slots(struct frame_pacer_metrics *metrics,
                                   uint64_t sample_ns)
{
    unsigned int index;

    for (index = 0; index < FRAME_PACER_THREAD_CPU_SLOTS_MAX; ++index) {
        struct frame_pacer_thread_cpu_sample *sample = &metrics->thread_cpu[index];

        if (sample->started && sample->sample_ns != sample_ns)
            memset(sample, 0, sizeof(*sample));
    }
}

#ifdef FRAME_PACER_TEST
void frame_pacer_metrics_test_prune_thread_cpu_slots(
    struct frame_pacer_metrics *metrics, uint64_t sample_ns)
{
    prune_thread_cpu_slots(metrics, sample_ns);
}
#endif

static void sample_thread_cpu(struct frame_pacer_metrics *metrics,
                              uint64_t now_ns,
                              struct frame_pacer_metrics_snapshot *snapshot)
{
    char root[PATH_MAX]; DIR *directory; struct dirent *entry; unsigned int peak = 0;
    bool available = false, reset = false;

    if (!now_ns)
        goto cached;
    if (!thread_cgroup_root(root, sizeof(root)) || !(directory = opendir(root))) {
        invalidate_thread_cpu(metrics);
        return;
    }
    if (metrics->thread_cpu_sample_ns &&
        now_ns - metrics->thread_cpu_sample_ns < THREAD_CPU_SAMPLE_INTERVAL_NS) {
        (void)closedir(directory);
        goto cached;
    }
    metrics->thread_cpu_sample_ns = now_ns;
    while ((entry = readdir(directory))) {
        char *end; unsigned long value; uint64_t usage_usec;
        struct frame_pacer_thread_cpu_sample *previous;
        unsigned int percent;

        if (strncmp(entry->d_name, "t-", 2) ||
            !(value = strtoul(entry->d_name + 2, &end, 10)) || *end ||
            value > UINT32_MAX ||
            !child_usage_usec(root, entry->d_name, &usage_usec))
            continue;
        previous = thread_cpu_slot(metrics, (uint32_t)value);
        if (!previous) continue;
        if (previous->started && now_ns > previous->sample_ns) {
            if (usage_usec < previous->usage_usec) {
                reset = true;
            } else {
                uint64_t elapsed = now_ns - previous->sample_ns;
                uint64_t delta = usage_usec - previous->usage_usec;
                if (delta > UINT64_MAX / UINT64_C(1000)) {
                    percent = 100;
                    available = true;
                } else {
                    available = frame_pacer_drm_fdinfo_utilisation(
                        0, delta * UINT64_C(1000), elapsed, &percent);
                }
                if (available && percent > peak) peak = percent;
            }
        }
        previous->tid = (uint32_t)value;
        previous->usage_usec = usage_usec;
        previous->sample_ns = now_ns;
        previous->started = true;
    }
    (void)closedir(directory);
    prune_thread_cpu_slots(metrics, now_ns);
    if (reset || !available) {
        metrics->thread_cpu_available = false;
        return;
    }
    metrics->thread_cpu_percent = peak;
    metrics->thread_cpu_available = true;
cached:
    if (metrics->thread_cpu_available) {
        snapshot->thread_cpu_percent = metrics->thread_cpu_percent;
        snapshot->available |= FRAME_PACER_METRIC_THREAD_CPU_USE;
    }
}

bool frame_pacer_metrics_parse_render_node(const char *target, char *node, size_t node_size)
{
    const char *base;
    int written;

    if (!target || !node || !node_size)
        return false;
    base = strrchr(target, '/');
    base = base ? base + 1 : target;
    if (strncmp(base, "renderD", 7) || !base[7] ||
        strspn(base + 7, "0123456789") != strlen(base + 7))
        return false;
    written = snprintf(node, node_size, "%s", base);
    return written >= 0 && (size_t)written < node_size;
}

static void find_process_gpu(struct frame_pacer_metrics *metrics, unsigned int process_id)
{
    DIR *directory;
    struct dirent *entry;
    char directory_path[64];
    int written;

    written = snprintf(directory_path, sizeof(directory_path), "/proc/%u/fd",
                       process_id);
    if (!process_id || written < 0 ||
        (size_t)written >= sizeof(directory_path))
        return;
    directory = opendir(directory_path);
    if (!directory) return;
    while ((entry = readdir(directory))) {
        char fd_path[96];
        char target[PATH_MAX];
        ssize_t length;

        if (entry->d_name[0] == '.')
            continue;
        written = snprintf(fd_path, sizeof(fd_path), "%s/%s", directory_path,
                           entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(fd_path))
            continue;
        length = readlink(fd_path, target, sizeof(target) - 1);
        if (length < 0)
            continue;
        target[length] = '\0';
        if (frame_pacer_metrics_parse_render_node(
                target, metrics->gpu_render_node,
                sizeof(metrics->gpu_render_node)))
            break;
    }
    (void)closedir(directory);
}

static void find_cpu_temperature(struct frame_pacer_metrics *metrics)
{
    DIR *directory;
    struct dirent *entry;
    directory = opendir("/sys/class/hwmon");
    if (!directory) return;
    while ((entry = readdir(directory))) {
        FILE *file;
        char path[256], name[64], label[64];
        int index;
        int written;
        if (entry->d_name[0] == '.')
            continue;
        written = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name",
                           entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;
        file = fopen(path, "re");
        if (!file || !fgets(name, sizeof(name), file)) {
            if (file)
                (void)fclose(file);
            continue;
        }
        (void)fclose(file);
        name[strcspn(name, "\r\n")] = '\0';
        if (strcmp(name, "coretemp")) continue;
        for (index = 1; index <= 16; ++index) {
            written = snprintf(path, sizeof(path),
                               "/sys/class/hwmon/%s/temp%d_label",
                               entry->d_name, index);
            if (written < 0 || (size_t)written >= sizeof(path))
                break;
            file = fopen(path, "re");
            if (!file || !fgets(label, sizeof(label), file)) {
                if (file)
                    (void)fclose(file);
                continue;
            }
            (void)fclose(file);
            label[strcspn(label, "\r\n")] = '\0';
            if (!strcmp(label, "Package id 0")) {
                written = snprintf(metrics->cpu_temp_path,
                                   sizeof(metrics->cpu_temp_path),
                                   "/sys/class/hwmon/%s/temp%d_input",
                                   entry->d_name, index);
                if (written >= 0 &&
                    (size_t)written < sizeof(metrics->cpu_temp_path)) {
                    (void)closedir(directory);
                    return;
                }
                metrics->cpu_temp_path[0] = '\0';
            }
        }
    }
    (void)closedir(directory);
}

void frame_pacer_metrics_init(struct frame_pacer_metrics *metrics,
                              const char *library, unsigned int process_id)
{
    void *symbol;
    int (*init)(void) = 0;
    if (!metrics) return;
    memset(metrics, 0, sizeof(*metrics));
    if (pthread_mutex_init(&metrics->mutex, 0)) return;
    metrics->initialized = true;
    find_cpu_temperature(metrics);
    if (!process_id) return;
    metrics->process_id = process_id;
    find_process_gpu(metrics, process_id);
    metrics->nvml_library =
        dlopen(library && *library ? library : "libnvidia-ml.so.1",
               RTLD_LAZY | RTLD_LOCAL);
    if (!metrics->nvml_library)
        return;
    symbol = dlsym(metrics->nvml_library, "nvmlInit_v2");
    function_from_symbol(symbol, &init, sizeof(init));
    symbol = dlsym(metrics->nvml_library, "nvmlShutdown");
    function_from_symbol(symbol, &metrics->nvml_shutdown,
                         sizeof(metrics->nvml_shutdown));
    symbol = dlsym(metrics->nvml_library, "nvmlDeviceGetCount_v2");
    function_from_symbol(symbol, &metrics->nvml_get_count,
                         sizeof(metrics->nvml_get_count));
    symbol = dlsym(metrics->nvml_library, "nvmlDeviceGetHandleByIndex_v2");
    function_from_symbol(symbol, &metrics->nvml_get_device,
                         sizeof(metrics->nvml_get_device));
    symbol = dlsym(metrics->nvml_library,
                   "nvmlDeviceGetGraphicsRunningProcesses");
    function_from_symbol(symbol, &metrics->nvml_get_graphics_processes,
                         sizeof(metrics->nvml_get_graphics_processes));
    symbol = dlsym(metrics->nvml_library, "nvmlDeviceGetUtilizationRates");
    function_from_symbol(symbol, &metrics->nvml_utilization,
                         sizeof(metrics->nvml_utilization));
    symbol = dlsym(metrics->nvml_library, "nvmlDeviceGetTemperature");
    function_from_symbol(symbol, &metrics->nvml_temperature,
                         sizeof(metrics->nvml_temperature));
    if (!init || !metrics->nvml_shutdown || !metrics->nvml_get_count ||
        !metrics->nvml_get_device || !metrics->nvml_get_graphics_processes ||
        !metrics->nvml_utilization || !metrics->nvml_temperature)
        goto unavailable;
    if (init() != 0) goto unavailable;
    metrics->nvml_started = true;
    frame_pacer_metrics_select_gpu(metrics, process_id);
    return;
unavailable:
    metrics->nvml_started = false;
    (void)dlclose(metrics->nvml_library);
    metrics->nvml_library = 0;
}

void frame_pacer_metrics_select_gpu(struct frame_pacer_metrics *metrics, unsigned int process_id)
{
    unsigned int count, index;
    if (!metrics || !metrics->initialized || !process_id)
        return;
    (void)pthread_mutex_lock(&metrics->mutex);
    metrics->nvml_device = 0;
    if (!metrics->gpu_render_node[0])
        find_process_gpu(metrics, process_id);
    if (!metrics->nvml_started || metrics->nvml_get_count(&count) != 0 ||
        count > 64)
        goto done;
    for (index = 0; index < count; ++index) {
        unsigned int process_count = 0, process_index, capacity;
        struct nvml_process_info *processes;
        void *device = 0;
        /* NVML reports the exact required count through this initial query. */
        if (metrics->nvml_get_device(index, &device) != 0 || !device)
            continue;
        (void)metrics->nvml_get_graphics_processes(device, &process_count, 0);
        if (!process_count || process_count > NVML_MAX_GRAPHICS_PROCESSES)
            continue;
        processes = calloc(process_count, sizeof(*processes));
        if (!processes)
            continue;
        capacity = process_count;
        if (metrics->nvml_get_graphics_processes(device, &process_count,
                                                  processes) == 0) {
            if (process_count > capacity) process_count = capacity;
            for (process_index = 0; process_index < process_count; ++process_index) {
                if (processes[process_index].pid == process_id) {
                    metrics->nvml_device = device;
                    break;
                }
            }
        }
        free(processes);
        if (metrics->nvml_device)
            break;
    }
done:
    (void)pthread_mutex_unlock(&metrics->mutex);
}

void frame_pacer_metrics_destroy(struct frame_pacer_metrics *metrics)
{
    if (!metrics || !metrics->initialized) return;
    metrics->initialized = false;
    if (metrics->nvml_started) (void)metrics->nvml_shutdown();
    if (metrics->nvml_library) (void)dlclose(metrics->nvml_library);
    (void)pthread_mutex_destroy(&metrics->mutex);
}

static bool nvml_retry_due(struct frame_pacer_metrics *metrics, uint64_t now_ns)
{
    bool retry = false;
    (void)pthread_mutex_lock(&metrics->mutex);
    if (metrics->nvml_started && !metrics->nvml_device &&
        (!metrics->nvml_retry_ns || now_ns - metrics->nvml_retry_ns >= NVML_RETRY_INTERVAL_NS)) {
        metrics->nvml_retry_ns = now_ns;
        retry = true;
    }
    (void)pthread_mutex_unlock(&metrics->mutex);
    return retry;
}

void frame_pacer_metrics_sample(struct frame_pacer_metrics *metrics,
                                struct frame_pacer_metrics_snapshot *snapshot)
{
    FILE *file;
    char line[256];
    uint64_t total, idle;
    struct nvml_utilization utilization;
    struct timespec now;
    uint64_t now_ns = 0;

    if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
    if (!metrics || !metrics->initialized || !snapshot) return;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        now_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    if (now_ns && nvml_retry_due(metrics, now_ns))
        frame_pacer_metrics_select_gpu(metrics, metrics->process_id);
    (void)pthread_mutex_lock(&metrics->mutex);
    file = fopen("/proc/stat", "re");
    if (file && fgets(line, sizeof(line), file) &&
        frame_pacer_metrics_parse_cpu(line, &total, &idle)) {
        if (metrics->cpu_started && total > metrics->cpu_total && idle >= metrics->cpu_idle) {
            uint64_t total_delta = total - metrics->cpu_total;
            uint64_t idle_delta = idle - metrics->cpu_idle;
            uint64_t bounded_idle = idle_delta > total_delta ? total_delta : idle_delta;

            if (frame_pacer_drm_fdinfo_utilisation(
                    0, total_delta - bounded_idle, total_delta,
                    &snapshot->cpu_use_percent))
                snapshot->available |= FRAME_PACER_METRIC_CPU_USE;
        }
        metrics->cpu_total = total;
        metrics->cpu_idle = idle;
        metrics->cpu_started = true;
    }
    if (file) (void)fclose(file);
    sample_thread_cpu(metrics, now_ns, snapshot);
    if (metrics->cpu_temp_path[0] && (file = fopen(metrics->cpu_temp_path, "re"))) {
        if (fgets(line, sizeof(line), file) &&
            parse_temperature(line, &snapshot->cpu_temp_celsius)) {
            snapshot->available |= FRAME_PACER_METRIC_CPU_TEMP;
        }
        (void)fclose(file);
    }
    if (metrics->nvml_device &&
        metrics->nvml_utilization(metrics->nvml_device, &utilization) == 0 &&
        utilization.gpu <= 100) {
        snapshot->gpu_use_percent = utilization.gpu;
        snapshot->available |= FRAME_PACER_METRIC_GPU_USE;
    }
    if (metrics->nvml_device &&
        metrics->nvml_temperature(metrics->nvml_device, 0,
                                  &snapshot->gpu_temp_celsius) == 0 &&
        snapshot->gpu_temp_celsius <= 200)
        snapshot->available |= FRAME_PACER_METRIC_GPU_TEMP;
    if (!metrics->nvml_device && metrics->gpu_render_node[0] && now_ns &&
        frame_pacer_drm_fdinfo_sample(&metrics->drm_fdinfo,
                                      metrics->process_id,
                                      metrics->gpu_render_node, now_ns,
                                      &snapshot->gpu_use_percent))
        snapshot->available |= FRAME_PACER_METRIC_GPU_USE;
    (void)pthread_mutex_unlock(&metrics->mutex);
}
