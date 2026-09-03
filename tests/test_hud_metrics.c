#define _POSIX_C_SOURCE 200809L
#include "hud_metrics.h"
#include "hud_nvml_client.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fixture_path(char *output, size_t size, const char *root,
                         const char *suffix)
{
    int written = snprintf(output, size, "%s/%s", root, suffix);

    assert(written >= 0 && (size_t)written < size);
}

static void fixture_directory(const char *root, const char *suffix)
{
    char path[512];

    fixture_path(path, sizeof(path), root, suffix);
    assert(!mkdir(path, 0700));
}

static void fixture_file(const char *root, const char *suffix,
                         const char *contents)
{
    char path[512];
    FILE *file;

    fixture_path(path, sizeof(path), root, suffix);
    file = fopen(path, "we");
    assert(file);
    assert(fputs(contents, file) >= 0);
    assert(!fclose(file));
}

static void fixture_symlink(const char *root, const char *suffix,
                            const char *target)
{
    char path[512];

    fixture_path(path, sizeof(path), root, suffix);
    assert(!symlink(target, path));
}

static void fixture_remove(const char *root, const char *suffix, bool directory)
{
    char path[512];

    fixture_path(path, sizeof(path), root, suffix);
    assert(!(directory ? rmdir(path) : unlink(path)));
}

static void cpu_parser(void)
{
    uint64_t total, idle;
    assert(frame_pacer_metrics_parse_cpu("cpu  100 20 30 400 50 10 20 5 0 0\n",
                                         &total, &idle));
    assert(total == 635 && idle == 450);
    assert(frame_pacer_metrics_parse_cpu("cpu12 100 20 30 400 50 10 20 5\n",
                                         &total, &idle));
    assert(total == 635 && idle == 450);
    assert(!frame_pacer_metrics_parse_cpu("intr 1 2 3\n", &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu(
        "cpu 18446744073709551615 1 0 0 0 0 0 0\n", &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu -1 0 0 0 0 0 0 0\n", &total,
                                          &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu +1 0 0 0 0 0 0 0\n", &total,
                                          &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7 8 junk\n", &total,
                                          &idle));
    assert(
        !frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7\n", &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7 8\n", 0, &idle));
    assert(frame_pacer_metrics_parse_cpu_stat("usage_usec 12345\n",
                                              "usage_usec", &total));
    assert(total == 12345);
    assert(!frame_pacer_metrics_parse_cpu_stat("throttled_usec 12345\n",
                                               "usage_usec", &total));
    assert(!frame_pacer_metrics_parse_cpu_stat("usage_usec -1\n", "usage_usec",
                                               &total));
    assert(!frame_pacer_metrics_parse_cpu_stat("usage_usec 1 trailing\n",
                                               "usage_usec", &total));
    assert(!frame_pacer_metrics_parse_cpu_stat(
        "usage_usec 18446744073709551616\n", "usage_usec", &total));
}

static void render_node_parser(void)
{
    char node[32];
    char pci[16];
    unsigned int vendor;

    assert(frame_pacer_metrics_parse_render_node("/dev/dri/renderD128", node,
                                                 sizeof(node)));
    assert(!strcmp(node, "renderD128"));
    assert(!frame_pacer_metrics_parse_render_node("/dev/dri/card1", node,
                                                  sizeof(node)));
    assert(frame_pacer_metrics_test_parse_gpu_vendor("0x10de\n", &vendor));
    assert(vendor == 0x10deU);
    assert(!frame_pacer_metrics_test_parse_gpu_vendor("10de\n", &vendor));
    assert(!frame_pacer_metrics_test_parse_gpu_vendor("0x10de junk", &vendor));
    assert(frame_pacer_metrics_test_parse_pci_bus_id(
        "/sys/devices/pci0000:00/0000:01:00.0", pci, sizeof(pci)));
    assert(!strcmp(pci, "0000:01:00.0"));
    assert(!frame_pacer_metrics_test_parse_pci_bus_id(
        "/sys/devices/pci0000:00/0000:01:00", pci, sizeof(pci)));
}

static void temperature_parser(void)
{
    unsigned int temperature = 0;

    assert(frame_pacer_metrics_test_parse_temperature("64500\n", &temperature));
    assert(temperature == 65);
    assert(
        frame_pacer_metrics_test_parse_temperature("200000\n", &temperature));
    assert(temperature == 200);
    assert(
        !frame_pacer_metrics_test_parse_temperature("200001\n", &temperature));
    assert(
        !frame_pacer_metrics_test_parse_temperature("-1000\n", &temperature));
    assert(
        !frame_pacer_metrics_test_parse_temperature("+1000\n", &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("1000 junk\n",
                                                       &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("", &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("1000", 0));
}

static void drm_fdinfo_parser(void)
{
    uint64_t value;
    unsigned int percent;
    assert(frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\t15514652816 ns\n", &value));
    assert(value == UINT64_C(15514652816));
    assert(frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-gfx:\t3706467 ns\n", &value));
    assert(value == UINT64_C(3706467));
    assert(frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-compute:\t101456 ns\n", &value));
    assert(value == UINT64_C(101456));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-copy:\t1 ns\n",
                                                   &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-dma:\t1 ns\n",
                                                   &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\tbad ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-gfx:\t1 us\n",
                                                   &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-compute-extra:\t1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-gfx:\t18446744073709551616 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\t-1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\t1 ns\ntrailing", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\t+1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\t1 ns\n",
                                                   0));
    assert(frame_pacer_drm_fdinfo_utilisation(100, 150, 200, &percent) &&
           percent == 25);
    assert(frame_pacer_drm_fdinfo_utilisation(100, 500, 200, &percent) &&
           percent == 100);
    assert(!frame_pacer_drm_fdinfo_utilisation(500, 100, 200, &percent));
    assert(frame_pacer_drm_fdinfo_utilisation(0, UINT64_MAX - 1, UINT64_MAX,
                                              &percent) &&
           percent == 100);
    assert(frame_pacer_drm_fdinfo_utilisation(0, 1, UINT64_MAX, &percent) &&
           percent == 0);
}

static void drm_fdinfo_amd_fixture(void)
{
    struct frame_pacer_drm_fdinfo state = {0};
    unsigned char committed[sizeof(state.clients)];
    unsigned char current[sizeof(state.clients)];
    char root[] = "/tmp/frame-pacer-proc-XXXXXX";
    unsigned int percent;

    assert(mkdtemp(root));
    fixture_directory(root, "42");
    fixture_directory(root, "42/fd");
    fixture_directory(root, "42/fdinfo");
    fixture_symlink(root, "42/fd/3", "/dev/dri/renderD128");
    fixture_symlink(root, "42/fd/4", "/dev/dri/renderD128");
    fixture_symlink(root, "42/fd/5", "/dev/dri/renderD128");
    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t100 ns\n"
                 "drm-engine-compute:\t50 ns\n"
                 "drm-engine-dma:\t9999 ns\n");
    fixture_file(root, "42/fdinfo/4",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t100 ns\n"
                 "drm-engine-compute:\t50 ns\n");
    fixture_file(root, "42/fdinfo/5",
                 "drm-client-id:\t8\n"
                 "drm-engine-gfx:\t25 ns\n");
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 1000, &percent, root));

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t500 ns\n"
                 "drm-engine-compute:\t250 ns\n");
    fixture_file(root, "42/fdinfo/4",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t450 ns\n"
                 "drm-engine-compute:\t200 ns\n");
    fixture_file(root, "42/fdinfo/5",
                 "drm-client-id:\t8\n"
                 "drm-engine-gfx:\t125 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 2000, &percent, root));
    assert(percent == 50);

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t600 ns\n");
    fixture_file(root, "42/fdinfo/4",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t590 ns\n");
    fixture_remove(root, "42/fdinfo/5", false);
    fixture_remove(root, "42/fd/5", false);
    fixture_symlink(root, "42/fd/6", "/dev/dri/renderD128");
    fixture_file(root, "42/fdinfo/6",
                 "drm-client-id:\t9\n"
                 "drm-engine-gfx:\t10000 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 3000, &percent, root));
    assert(percent == 10);

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t700 ns\n"
                 "drm-engine-compute:\t1000 ns\n");
    fixture_file(root, "42/fdinfo/4",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t690 ns\n"
                 "drm-engine-compute:\t900 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 4000, &percent, root));
    assert(percent == 10);

    fixture_remove(root, "42/fdinfo/6", false);
    fixture_remove(root, "42/fd/6", false);
    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t650 ns\n"
                 "drm-engine-compute:\t1100 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 5000, &percent, root));
    assert(percent == 10);

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t750 ns\n"
                 "drm-engine-compute:\t1200 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 6000, &percent, root));
    assert(percent == 10);

    memcpy(committed, state.clients, sizeof(committed));
    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\tinvalid\n"
                 "drm-engine-gfx:\t800 ns\n");
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 7000, &percent, root));
    assert(state.previous_sample_ns == 6000);
    /* Compare object representations: failure must not change even padding. */
    memcpy(current, state.clients, sizeof(current));
    assert(!memcmp(committed, current, sizeof(committed)));

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t18446744073709551616\n"
                 "drm-engine-gfx:\t800 ns\n");
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 7500, &percent, root));
    assert(state.previous_sample_ns == 6000);
    memcpy(current, state.clients, sizeof(current));
    assert(!memcmp(committed, current, sizeof(committed)));

    fixture_file(root, "42/fdinfo/3",
                 "drm-client-id:\t7\n"
                 "drm-engine-gfx:\t850 ns\n"
                 "drm-engine-compute:\t1400 ns\n");
    assert(frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 8000, &percent, root));
    assert(percent == 10);

    fixture_remove(root, "42/fdinfo/4", false);
    fixture_remove(root, "42/fdinfo/3", false);
    fixture_remove(root, "42/fd/4", false);
    fixture_remove(root, "42/fd/3", false);
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 42, "renderD128", 9000, &percent, root));
    assert(state.started && !state.clients[0].used);
    fixture_remove(root, "42/fdinfo", true);
    fixture_remove(root, "42/fd", true);
    fixture_remove(root, "42", true);
    assert(!rmdir(root));
}

static void drm_fdinfo_limits_are_transactional(void)
{
    struct frame_pacer_drm_fdinfo state = {0};
    char root[] = "/tmp/frame-pacer-proc-limits-XXXXXX";
    char overflow_root[] = "/tmp/frame-pacer-proc-overflow-XXXXXX";
    char suffix[64], contents[128];
    unsigned int index, percent;

    assert(mkdtemp(root));
    fixture_directory(root, "43");
    fixture_directory(root, "43/fd");
    fixture_directory(root, "43/fdinfo");
    for (index = 0; index <= FRAME_PACER_DRM_FDINFO_MAX_CLIENTS; ++index) {
        assert(snprintf(suffix, sizeof(suffix), "43/fd/%u", index + 3) > 0);
        fixture_symlink(root, suffix, "/dev/dri/renderD128");
        assert(snprintf(suffix, sizeof(suffix), "43/fdinfo/%u", index + 3) > 0);
        assert(snprintf(contents, sizeof(contents),
                        "drm-client-id: %u\ndrm-engine-gfx: 0 ns\n",
                        index + 1) > 0);
        fixture_file(root, suffix, contents);
    }
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 43, "renderD128", 1000, &percent, root));
    assert(!state.started);
    for (index = 0; index <= FRAME_PACER_DRM_FDINFO_MAX_CLIENTS; ++index) {
        assert(snprintf(suffix, sizeof(suffix), "43/fdinfo/%u", index + 3) > 0);
        fixture_remove(root, suffix, false);
        assert(snprintf(suffix, sizeof(suffix), "43/fd/%u", index + 3) > 0);
        fixture_remove(root, suffix, false);
    }
    fixture_remove(root, "43/fdinfo", true);
    fixture_remove(root, "43/fd", true);
    fixture_remove(root, "43", true);
    assert(!rmdir(root));

    memset(&state, 0, sizeof(state));
    assert(mkdtemp(overflow_root));
    fixture_directory(overflow_root, "44");
    fixture_directory(overflow_root, "44/fd");
    fixture_directory(overflow_root, "44/fdinfo");
    fixture_symlink(overflow_root, "44/fd/3", "/dev/dri/renderD128");
    fixture_symlink(overflow_root, "44/fd/4", "/dev/dri/renderD128");
    fixture_file(overflow_root, "44/fdinfo/3",
                 "drm-client-id: 1\ndrm-engine-gfx: 0 ns\n");
    fixture_file(overflow_root, "44/fdinfo/4",
                 "drm-client-id: 2\ndrm-engine-gfx: 0 ns\n");
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 44, "renderD128", 1000, &percent, overflow_root));
    assert(state.started && state.previous_sample_ns == 1000);
    fixture_file(overflow_root, "44/fdinfo/3",
                 "drm-client-id: 1\n"
                 "drm-engine-gfx: 18446744073709551615 ns\n");
    fixture_file(overflow_root, "44/fdinfo/4",
                 "drm-client-id: 2\n"
                 "drm-engine-gfx: 18446744073709551615 ns\n");
    assert(!frame_pacer_drm_fdinfo_test_sample_from_root(
        &state, 44, "renderD128", 2000, &percent, overflow_root));
    assert(state.previous_sample_ns == 1000);
    assert(state.clients[0].engines[1].high_water_ns == 0);
    fixture_remove(overflow_root, "44/fdinfo/4", false);
    fixture_remove(overflow_root, "44/fdinfo/3", false);
    fixture_remove(overflow_root, "44/fd/4", false);
    fixture_remove(overflow_root, "44/fd/3", false);
    fixture_remove(overflow_root, "44/fdinfo", true);
    fixture_remove(overflow_root, "44/fd", true);
    fixture_remove(overflow_root, "44", true);
    assert(!rmdir(overflow_root));
}

static void amd_temperature_fixture(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    char root[] = "/tmp/frame-pacer-drm-XXXXXX";

    assert(mkdtemp(root));
    fixture_directory(root, "renderD128");
    fixture_directory(root, "renderD128/device");
    fixture_directory(root, "renderD128/device/hwmon");
    fixture_directory(root, "renderD128/device/hwmon/hwmon0");
    fixture_file(root, "renderD128/device/hwmon/hwmon0/name", "amdgpu\r\n");
    fixture_file(root, "renderD128/device/hwmon/hwmon0/temp1_label",
                 "junction\n");
    fixture_file(root, "renderD128/device/hwmon/hwmon0/temp1_input", "70000\n");
    fixture_file(root, "renderD128/device/hwmon/hwmon0/temp2_label",
                 "edge\r\n");
    fixture_file(root, "renderD128/device/hwmon/hwmon0/temp2_input", "41000\n");
    fixture_directory(root, "renderD129");
    fixture_directory(root, "renderD129/device");
    fixture_directory(root, "renderD129/device/hwmon");
    fixture_directory(root, "renderD129/device/hwmon/hwmon1");
    fixture_file(root, "renderD129/device/hwmon/hwmon1/name", "radeon\n");
    fixture_file(root, "renderD129/device/hwmon/hwmon1/temp1_input", "52000\n");

    frame_pacer_metrics_init(&metrics, 0, 0);
    frame_pacer_metrics_test_set_gpu_identity(&metrics, "renderD128", 0x1002U,
                                              "0000:01:00.0");
    assert(frame_pacer_metrics_test_find_amd_temperature(&metrics, root));
    assert(strstr(metrics.gpu_temp_path,
                  "renderD128/device/hwmon/hwmon0/temp2_input"));
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(snapshot.available & FRAME_PACER_METRIC_GPU_TEMP);
    assert(snapshot.gpu_temp_celsius == 41);

    /* Unterminated/truncated sensor names cannot select a temperature path. */
    fixture_file(
        root, "renderD128/device/hwmon/hwmon0/name",
        "amdgpu_________________________________________________________"
        "truncated");
    assert(!frame_pacer_metrics_test_find_amd_temperature(&metrics, root));
    assert(!metrics.gpu_temp_path[0]);
    fixture_file(root, "renderD128/device/hwmon/hwmon0/name", "amdgpu");
    assert(frame_pacer_metrics_test_find_amd_temperature(&metrics, root));

    frame_pacer_metrics_test_set_gpu_identity(&metrics, "renderD129", 0x1002U,
                                              "0000:02:00.0");
    assert(frame_pacer_metrics_test_find_amd_temperature(&metrics, root));
    assert(strstr(metrics.gpu_temp_path,
                  "renderD129/device/hwmon/hwmon1/temp1_input"));
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(snapshot.available & FRAME_PACER_METRIC_GPU_TEMP);
    assert(snapshot.gpu_temp_celsius == 52);

    fixture_file(root, "renderD129/device/hwmon/hwmon1/temp1_input", "bad\n");
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(!(snapshot.available & FRAME_PACER_METRIC_GPU_TEMP));
    fixture_file(root, "renderD129/device/hwmon/hwmon1/temp1_input",
                 "200001\n");
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(!(snapshot.available & FRAME_PACER_METRIC_GPU_TEMP));

    frame_pacer_metrics_test_set_gpu_identity(&metrics, "renderD128", 0x8086U,
                                              "0000:01:00.0");
    assert(!frame_pacer_metrics_test_find_amd_temperature(&metrics, root));
    assert(!metrics.gpu_temp_path[0]);
    frame_pacer_metrics_test_set_gpu_identity(&metrics, "renderD130", 0x1002U,
                                              "0000:03:00.0");
    assert(!frame_pacer_metrics_test_find_amd_temperature(&metrics, root));
    assert(!metrics.gpu_temp_path[0]);
    frame_pacer_metrics_destroy(&metrics);

    fixture_remove(root, "renderD129/device/hwmon/hwmon1/temp1_input", false);
    fixture_remove(root, "renderD129/device/hwmon/hwmon1/name", false);
    fixture_remove(root, "renderD129/device/hwmon/hwmon1", true);
    fixture_remove(root, "renderD129/device/hwmon", true);
    fixture_remove(root, "renderD129/device", true);
    fixture_remove(root, "renderD129", true);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0/temp2_input", false);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0/temp2_label", false);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0/temp1_input", false);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0/temp1_label", false);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0/name", false);
    fixture_remove(root, "renderD128/device/hwmon/hwmon0", true);
    fixture_remove(root, "renderD128/device/hwmon", true);
    fixture_remove(root, "renderD128/device", true);
    fixture_remove(root, "renderD128", true);
    assert(!rmdir(root));
}

static void vanished_thread_slots_are_reusable(void)
{
    struct frame_pacer_metrics metrics = {0};

    metrics.thread_cpu[0] = (struct frame_pacer_thread_cpu_sample){
        .tid = 10,
        .sample_ns = 1,
        .started = true,
    };
    metrics.thread_cpu[1] = (struct frame_pacer_thread_cpu_sample){
        .tid = 11,
        .sample_ns = 2,
        .started = true,
    };
    frame_pacer_metrics_test_prune_thread_cpu_slots(&metrics, 2);
    assert(!metrics.thread_cpu[0].started);
    assert(metrics.thread_cpu[1].started && metrics.thread_cpu[1].tid == 11);
}

static void utilization_reset_preserves_provider_identity(void)
{
    struct frame_pacer_metrics metrics;

    frame_pacer_metrics_init(&metrics, 0, 0);
    metrics.cpu_total = 100;
    metrics.cpu_idle = 50;
    metrics.cpu_started = true;
    metrics.thread_cpu[0] = (struct frame_pacer_thread_cpu_sample){
        .tid = 10,
        .usage_usec = 20,
        .sample_ns = 30,
        .started = true,
    };
    metrics.thread_cpu_sample_ns = 30;
    metrics.thread_cpu_available = true;
    metrics.drm_fdinfo.started = true;
    metrics.drm_fdinfo.clients[0].used = true;
    assert(snprintf(metrics.gpu_render_node, sizeof(metrics.gpu_render_node),
                    "renderD128") > 0);
    assert(snprintf(metrics.gpu_temp_path, sizeof(metrics.gpu_temp_path),
                    "/sensor/path") > 0);

    frame_pacer_metrics_reset_utilization(&metrics);
    assert(!metrics.cpu_started && !metrics.cpu_total && !metrics.cpu_idle);
    assert(!metrics.thread_cpu[0].started && !metrics.thread_cpu_sample_ns &&
           !metrics.thread_cpu_available);
    assert(!metrics.drm_fdinfo.started && !metrics.drm_fdinfo.clients[0].used);
    assert(!strcmp(metrics.gpu_render_node, "renderD128"));
    assert(!strcmp(metrics.gpu_temp_path, "/sensor/path"));
    frame_pacer_metrics_destroy(&metrics);
}

static void nvml_backend(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    char temperature_path[] = "/tmp/frame-pacer-nvml-temp-XXXXXX";
    int temperature_fd;

    frame_pacer_metrics_init(&metrics, "build/test-nvml.so",
                             (unsigned int)getpid());
    assert(!metrics.nvml.device);
    temperature_fd = mkstemp(temperature_path);
    assert(temperature_fd >= 0);
    assert(write(temperature_fd, "1000\n", 5) == 5);
    assert(!close(temperature_fd));
    assert(snprintf(metrics.gpu_temp_path, sizeof(metrics.gpu_temp_path), "%s",
                    temperature_path) > 0);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(metrics.nvml.device);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_USE) &&
           snapshot.gpu_use_percent == 37);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_TEMP) &&
           snapshot.gpu_temp_celsius == 64);
    assert(frame_pacer_nvml_client_test_attempts() == 0);
    frame_pacer_metrics_destroy(&metrics);
    frame_pacer_metrics_destroy(&metrics);
    assert(!unlink(temperature_path));
}

static void unavailable_is_safe(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    frame_pacer_metrics_init(&metrics, "build/test-nvml.so", UINT32_MAX);
    assert(!metrics.nvml.device);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(!(snapshot.available &
             (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)));
    frame_pacer_metrics_destroy(&metrics);
}

static void incomplete_nvml_is_not_shutdown_without_init(void)
{
    struct frame_pacer_metrics metrics;
    void *provider =
        dlopen("build/test-nvml-incomplete.so", RTLD_NOW | RTLD_LOCAL);
    unsigned int (*shutdown_calls)(void);
    void *symbol;

    assert(provider);
    symbol = dlsym(provider, "frame_pacer_test_nvml_shutdown_calls");
    memcpy(&shutdown_calls, &symbol, sizeof(shutdown_calls));
    assert(shutdown_calls && shutdown_calls() == 0);
    frame_pacer_metrics_init(&metrics, "build/test-nvml-incomplete.so",
                             (unsigned int)getpid());
    assert(!metrics.nvml.library && !metrics.nvml.started);
    assert(shutdown_calls() == 0);
    frame_pacer_metrics_destroy(&metrics);
    assert(shutdown_calls() == 0);
    dlclose(provider);
}

int main(void)
{
    frame_pacer_metrics_init(0, 0, 0);
    frame_pacer_metrics_destroy(0);
    cpu_parser();
    render_node_parser();
    temperature_parser();
    drm_fdinfo_parser();
    drm_fdinfo_amd_fixture();
    drm_fdinfo_limits_are_transactional();
    amd_temperature_fixture();
    vanished_thread_slots_are_reusable();
    utilization_reset_preserves_provider_identity();
    nvml_backend();
    unavailable_is_safe();
    incomplete_nvml_is_not_shutdown_without_init();
}
