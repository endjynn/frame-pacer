#define _POSIX_C_SOURCE 200809L
#include "hud_metrics.h"
#include "hud_nvml_client.h"
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NVML_RETRY_INTERVAL_NS UINT64_C(1000000000)
#define THREAD_CPU_SAMPLE_INTERVAL_NS UINT64_C(1000000000)

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

static bool parse_gpu_vendor(const char *text, unsigned int *vendor)
{
    char *end;
    unsigned long value;

    if (!text || !vendor || strncmp(text, "0x", 2) || !isxdigit(text[2]))
        return false;
    value = strtoul(text + 2, &end, 16);
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end || value > UINT32_MAX) return false;
    *vendor = (unsigned int)value;
    return true;
}

static bool parse_pci_bus_id(const char *target, char *pci, size_t size)
{
    static const unsigned int hex_positions[] = { 0, 1, 2, 3, 5, 6,
                                                   8, 9, 11 };
    const char *base;
    unsigned int index;

    if (!target || !pci || size < 13) return false;
    base = strrchr(target, '/');
    base = base ? base + 1 : target;
    if (strlen(base) != 12 || base[4] != ':' || base[7] != ':' ||
        base[10] != '.')
        return false;
    for (index = 0; index < sizeof(hex_positions) / sizeof(hex_positions[0]);
         ++index)
        if (!isxdigit((unsigned char)base[hex_positions[index]])) return false;
    memcpy(pci, base, 13);
    return true;
}

static void find_gpu_identity(struct frame_pacer_metrics *metrics)
{
    char path[128], line[64], target[PATH_MAX];
    FILE *file;
    ssize_t length;
    int written;

    metrics->gpu_vendor = 0;
    metrics->gpu_pci_bus_id[0] = '\0';
    if (!metrics->gpu_render_node[0]) return;
    written = snprintf(path, sizeof(path), "/sys/class/drm/%s/device/vendor",
                       metrics->gpu_render_node);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(file = fopen(path, "re")))
        return;
    if (!fgets(line, sizeof(line), file) ||
        !parse_gpu_vendor(line, &metrics->gpu_vendor))
        metrics->gpu_vendor = 0;
    (void)fclose(file);
    written = snprintf(path, sizeof(path), "/sys/class/drm/%s/device",
                       metrics->gpu_render_node);
    if (written < 0 || (size_t)written >= sizeof(path)) return;
    length = readlink(path, target, sizeof(target) - 1);
    if (length < 0) return;
    target[length] = '\0';
    (void)parse_pci_bus_id(
        target, metrics->gpu_pci_bus_id, sizeof(metrics->gpu_pci_bus_id));
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_metrics_test_parse_gpu_vendor(const char *text,
                                               unsigned int *vendor)
{
    return parse_gpu_vendor(text, vendor);
}

bool frame_pacer_metrics_test_parse_pci_bus_id(const char *target, char *pci,
                                               size_t size)
{
    return parse_pci_bus_id(target, pci, size);
}

void frame_pacer_metrics_test_set_gpu_identity(
    struct frame_pacer_metrics *metrics, const char *render_node,
    unsigned int vendor, const char *pci_bus_id)
{
    if (!metrics) return;
    (void)snprintf(metrics->gpu_render_node,
                   sizeof(metrics->gpu_render_node), "%s",
                   render_node ? render_node : "");
    (void)snprintf(metrics->gpu_pci_bus_id,
                   sizeof(metrics->gpu_pci_bus_id), "%s",
                   pci_bus_id ? pci_bus_id : "");
    metrics->gpu_temp_path[0] = '\0';
    metrics->gpu_vendor = vendor;
}
#endif

static bool read_trimmed_line(const char *path, char *output, size_t size)
{
    FILE *file;

    if (!path || !output || size < 2 || !(file = fopen(path, "re")))
        return false;
    if (!fgets(output, (int)size, file)) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    if (!strchr(output, '\n') && strlen(output) == size - 1)
        return false;
    output[strcspn(output, "\r\n")] = '\0';
    return true;
}

static bool store_temperature_path(struct frame_pacer_metrics *metrics,
                                   const char *path)
{
    int written;

    if (access(path, R_OK)) return false;
    written = snprintf(metrics->gpu_temp_path,
                       sizeof(metrics->gpu_temp_path), "%s", path);
    if (written >= 0 && (size_t)written < sizeof(metrics->gpu_temp_path))
        return true;
    metrics->gpu_temp_path[0] = '\0';
    return false;
}

static bool find_amd_temperature_at(struct frame_pacer_metrics *metrics,
                                    const char *drm_root)
{
    DIR *directory;
    struct dirent *entry;
    char directory_path[PATH_MAX], fallback[PATH_MAX] = "";
    int written;

    if (!metrics || !drm_root || !*drm_root) return false;
    metrics->gpu_temp_path[0] = '\0';
    if (metrics->gpu_vendor != 0x1002U || !metrics->gpu_render_node[0])
        return false;
    written = snprintf(directory_path, sizeof(directory_path),
                       "%s/%s/device/hwmon", drm_root,
                       metrics->gpu_render_node);
    if (written < 0 || (size_t)written >= sizeof(directory_path) ||
        !(directory = opendir(directory_path)))
        return false;
    while ((entry = readdir(directory))) {
        char path[PATH_MAX], name[64];
        unsigned int index;

        if (entry->d_name[0] == '.') continue;
        written = snprintf(path, sizeof(path), "%s/%s/name",
                           directory_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path) ||
            !read_trimmed_line(path, name, sizeof(name)) ||
            (strcmp(name, "amdgpu") && strcmp(name, "radeon")))
            continue;
        for (index = 1; index <= 3; ++index) {
            char input[PATH_MAX], label[64];

            written = snprintf(input, sizeof(input), "%s/%s/temp%u_input",
                               directory_path, entry->d_name, index);
            if (written < 0 || (size_t)written >= sizeof(input) ||
                access(input, R_OK))
                continue;
            if (index == 1 && !fallback[0])
                (void)snprintf(fallback, sizeof(fallback), "%s", input);
            written = snprintf(path, sizeof(path), "%s/%s/temp%u_label",
                               directory_path, entry->d_name, index);
            if (written >= 0 && (size_t)written < sizeof(path) &&
                read_trimmed_line(path, label, sizeof(label)) &&
                !strcmp(label, "edge")) {
                bool selected = store_temperature_path(metrics, input);

                (void)closedir(directory);
                return selected;
            }
        }
    }
    (void)closedir(directory);
    return fallback[0] && store_temperature_path(metrics, fallback);
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_metrics_test_find_amd_temperature(
    struct frame_pacer_metrics *metrics, const char *drm_root)
{
    return find_amd_temperature_at(metrics, drm_root);
}
#endif

static void find_gpu_temperature(struct frame_pacer_metrics *metrics)
{
    (void)find_amd_temperature_at(metrics, "/sys/class/drm");
}

static void find_process_gpu(struct frame_pacer_metrics *metrics, unsigned int process_id)
{
    DIR *directory;
    struct dirent *entry;
    char directory_path[64];
    char fallback_node[sizeof(metrics->gpu_render_node)] = "";
    char fallback_pci[sizeof(metrics->gpu_pci_bus_id)] = "";
    unsigned int fallback_vendor = 0;
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
        if (!frame_pacer_metrics_parse_render_node(
                target, metrics->gpu_render_node,
                sizeof(metrics->gpu_render_node)))
            continue;
        find_gpu_identity(metrics);
        if (metrics->gpu_vendor == 0x10deU) break;
        if (!fallback_node[0]) {
            (void)snprintf(fallback_node, sizeof(fallback_node), "%s",
                           metrics->gpu_render_node);
            (void)snprintf(fallback_pci, sizeof(fallback_pci), "%s",
                           metrics->gpu_pci_bus_id);
            fallback_vendor = metrics->gpu_vendor;
        }
    }
    (void)closedir(directory);
    if (metrics->gpu_vendor != 0x10deU && fallback_node[0]) {
        (void)snprintf(metrics->gpu_render_node,
                       sizeof(metrics->gpu_render_node), "%s",
                       fallback_node);
        (void)snprintf(metrics->gpu_pci_bus_id,
                       sizeof(metrics->gpu_pci_bus_id), "%s", fallback_pci);
        metrics->gpu_vendor = fallback_vendor;
    }
    find_gpu_temperature(metrics);
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
    if (!metrics) return;
    memset(metrics, 0, sizeof(*metrics));
    if (pthread_mutex_init(&metrics->mutex, 0)) return;
    metrics->initialized = true;
    find_cpu_temperature(metrics);
    if (!process_id) return;
    metrics->process_id = process_id;
    find_process_gpu(metrics, process_id);
    if (frame_pacer_nvml_provider_init(&metrics->nvml, library)) {
        ++metrics->nvml_select_attempts;
        (void)frame_pacer_nvml_provider_select_process(&metrics->nvml,
                                                       process_id);
    }
}

void frame_pacer_metrics_destroy(struct frame_pacer_metrics *metrics)
{
    if (!metrics || !metrics->initialized) return;
    metrics->initialized = false;
    if (metrics->nvml_external) frame_pacer_nvml_client_release();
    frame_pacer_nvml_provider_destroy(&metrics->nvml);
    (void)pthread_mutex_destroy(&metrics->mutex);
}

void frame_pacer_metrics_reset_utilization(struct frame_pacer_metrics *metrics)
{
    if (!metrics || !metrics->initialized) return;
    (void)pthread_mutex_lock(&metrics->mutex);
    metrics->cpu_total = 0;
    metrics->cpu_idle = 0;
    metrics->cpu_started = false;
    invalidate_thread_cpu(metrics);
    memset(&metrics->drm_fdinfo, 0, sizeof(metrics->drm_fdinfo));
    (void)pthread_mutex_unlock(&metrics->mutex);
}

static void refresh_gpu_provider(struct frame_pacer_metrics *metrics,
                                 uint64_t now_ns)
{
    (void)pthread_mutex_lock(&metrics->mutex);
    if ((!metrics->gpu_render_node[0] ||
         (metrics->nvml.started && !metrics->nvml.device)) &&
        (!metrics->nvml_retry_ns ||
         now_ns - metrics->nvml_retry_ns >= NVML_RETRY_INTERVAL_NS)) {
        metrics->nvml_retry_ns = now_ns;
        if (!metrics->gpu_render_node[0])
            find_process_gpu(metrics, metrics->process_id);
        if (metrics->nvml.started && !metrics->nvml.device) {
            ++metrics->nvml_select_attempts;
            (void)frame_pacer_nvml_provider_select_process(
                &metrics->nvml, metrics->process_id);
        }
    }
    if (!metrics->nvml_external && metrics->gpu_vendor == 0x10deU &&
        metrics->gpu_pci_bus_id[0] &&
        (!metrics->nvml.started ||
         (!metrics->nvml.device && metrics->nvml_select_attempts >= 3))) {
        metrics->nvml_external = frame_pacer_nvml_client_acquire(
            metrics->process_id, metrics->gpu_pci_bus_id);
    }
    (void)pthread_mutex_unlock(&metrics->mutex);
}

void frame_pacer_metrics_sample(struct frame_pacer_metrics *metrics,
                                struct frame_pacer_metrics_snapshot *snapshot)
{
    FILE *file;
    char line[256];
    uint64_t total, idle;
    struct frame_pacer_nvml_sample nvml_sample;
    struct frame_pacer_nvml_message external_message;
    struct timespec now;
    uint64_t now_ns = 0;

    if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
    if (!metrics || !metrics->initialized || !snapshot) return;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        now_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    if (now_ns) refresh_gpu_provider(metrics, now_ns);
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
    memset(&nvml_sample, 0, sizeof(nvml_sample));
    if (metrics->nvml.device)
        (void)frame_pacer_nvml_provider_sample(&metrics->nvml, &nvml_sample);
    else if (metrics->nvml_external &&
             frame_pacer_nvml_client_snapshot(&external_message))
        nvml_sample = external_message.sample;
    if (nvml_sample.available & FRAME_PACER_NVML_GPU_USE) {
        snapshot->gpu_use_percent = nvml_sample.gpu_use_percent;
        snapshot->available |= FRAME_PACER_METRIC_GPU_USE;
    }
    if (nvml_sample.available & FRAME_PACER_NVML_GPU_TEMP) {
        snapshot->gpu_temp_celsius = nvml_sample.gpu_temp_celsius;
        snapshot->available |= FRAME_PACER_METRIC_GPU_TEMP;
    }
    if (!(snapshot->available & FRAME_PACER_METRIC_GPU_TEMP) &&
        metrics->gpu_temp_path[0] &&
        (file = fopen(metrics->gpu_temp_path, "re"))) {
        if (fgets(line, sizeof(line), file) &&
            parse_temperature(line, &snapshot->gpu_temp_celsius))
            snapshot->available |= FRAME_PACER_METRIC_GPU_TEMP;
        (void)fclose(file);
    }
    if (!(snapshot->available & FRAME_PACER_METRIC_GPU_USE) &&
        metrics->gpu_render_node[0] && now_ns &&
        frame_pacer_drm_fdinfo_sample(&metrics->drm_fdinfo,
                                      metrics->process_id,
                                      metrics->gpu_render_node, now_ns,
                                      &snapshot->gpu_use_percent))
        snapshot->available |= FRAME_PACER_METRIC_GPU_USE;
    (void)pthread_mutex_unlock(&metrics->mutex);
}
