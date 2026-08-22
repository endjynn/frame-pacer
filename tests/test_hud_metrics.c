#include "hud_metrics.h"
#include "hud_nvml_client.h"
#include <assert.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

static void cpu_parser(void)
{
    uint64_t total, idle;
    assert(frame_pacer_metrics_parse_cpu("cpu  100 20 30 400 50 10 20 5 0 0\n", &total, &idle));
    assert(total == 635 && idle == 450);
    assert(frame_pacer_metrics_parse_cpu("cpu12 100 20 30 400 50 10 20 5\n", &total, &idle));
    assert(total == 635 && idle == 450);
    assert(!frame_pacer_metrics_parse_cpu("intr 1 2 3\n", &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu(
        "cpu 18446744073709551615 1 0 0 0 0 0 0\n", &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu -1 0 0 0 0 0 0 0\n",
                                          &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu +1 0 0 0 0 0 0 0\n",
                                          &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7 8 junk\n",
                                          &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7\n",
                                          &total, &idle));
    assert(!frame_pacer_metrics_parse_cpu("cpu 1 2 3 4 5 6 7 8\n", 0, &idle));
    assert(frame_pacer_metrics_parse_cpu_stat("usage_usec 12345\n", "usage_usec", &total));
    assert(total == 12345);
    assert(!frame_pacer_metrics_parse_cpu_stat("throttled_usec 12345\n", "usage_usec", &total));
    assert(!frame_pacer_metrics_parse_cpu_stat("usage_usec -1\n", "usage_usec", &total));
    assert(!frame_pacer_metrics_parse_cpu_stat("usage_usec 1 trailing\n", "usage_usec", &total));
    assert(!frame_pacer_metrics_parse_cpu_stat(
        "usage_usec 18446744073709551616\n", "usage_usec", &total));
}

static void render_node_parser(void)
{
    char node[32];
    char pci[16];
    unsigned int vendor;

    assert(frame_pacer_metrics_parse_render_node("/dev/dri/renderD128", node, sizeof(node)));
    assert(!strcmp(node, "renderD128"));
    assert(!frame_pacer_metrics_parse_render_node("/dev/dri/card1", node, sizeof(node)));
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

    assert(frame_pacer_metrics_test_parse_temperature("64500\n",
                                                       &temperature));
    assert(temperature == 65);
    assert(frame_pacer_metrics_test_parse_temperature("200000\n",
                                                       &temperature));
    assert(temperature == 200);
    assert(!frame_pacer_metrics_test_parse_temperature("200001\n",
                                                        &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("-1000\n",
                                                        &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("+1000\n",
                                                        &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("1000 junk\n",
                                                        &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("", &temperature));
    assert(!frame_pacer_metrics_test_parse_temperature("1000", 0));
}

static void drm_fdinfo_parser(void)
{
    uint64_t value;
    unsigned int percent;
    assert(frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\t15514652816 ns\n", &value));
    assert(value == UINT64_C(15514652816));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-copy:\t1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\tbad ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\t-1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns(
        "drm-engine-render:\t1 ns\ntrailing", &value));
    assert(frame_pacer_drm_fdinfo_utilisation(100, 150, 200, &percent) && percent == 25);
    assert(frame_pacer_drm_fdinfo_utilisation(100, 500, 200, &percent) && percent == 100);
    assert(!frame_pacer_drm_fdinfo_utilisation(500, 100, 200, &percent));
    assert(frame_pacer_drm_fdinfo_utilisation(0, UINT64_MAX - 1, UINT64_MAX,
                                               &percent) && percent == 100);
    assert(frame_pacer_drm_fdinfo_utilisation(0, 1, UINT64_MAX, &percent) &&
           percent == 0);
    {
        struct frame_pacer_drm_fdinfo state = {0};

        assert(!frame_pacer_drm_fdinfo_test_update_sample(&state, 100, 1000,
                                                           &percent));
        assert(frame_pacer_drm_fdinfo_test_update_sample(&state, 150, 1200,
                                                          &percent));
        assert(percent == 25 && state.available);
        assert(!frame_pacer_drm_fdinfo_test_update_sample(&state, 10, 1400,
                                                           &percent));
        assert(!state.available && state.cached_use_percent == 0);
    }
}

static void vanished_thread_slots_are_reusable(void)
{
    struct frame_pacer_metrics metrics = {0};

    metrics.thread_cpu[0] = (struct frame_pacer_thread_cpu_sample){
        .tid = 10, .sample_ns = 1, .started = true,
    };
    metrics.thread_cpu[1] = (struct frame_pacer_thread_cpu_sample){
        .tid = 11, .sample_ns = 2, .started = true,
    };
    frame_pacer_metrics_test_prune_thread_cpu_slots(&metrics, 2);
    assert(!metrics.thread_cpu[0].started);
    assert(metrics.thread_cpu[1].started && metrics.thread_cpu[1].tid == 11);
}

static void nvml_backend(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    frame_pacer_metrics_init(&metrics, "build/test-nvml.so", (unsigned int)getpid());
    assert(!metrics.nvml.device);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(metrics.nvml.device);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_USE) && snapshot.gpu_use_percent == 37);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_TEMP) && snapshot.gpu_temp_celsius == 64);
    assert(frame_pacer_nvml_client_test_attempts() == 0);
    frame_pacer_metrics_destroy(&metrics);
    frame_pacer_metrics_destroy(&metrics);
}

static void unavailable_is_safe(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    frame_pacer_metrics_init(&metrics, "build/test-nvml.so", UINT32_MAX);
    assert(!metrics.nvml.device);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(!(snapshot.available & (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)));
    frame_pacer_metrics_destroy(&metrics);
}

static void incomplete_nvml_is_not_shutdown_without_init(void)
{
    struct frame_pacer_metrics metrics;
    void *provider = dlopen("build/test-nvml-incomplete.so", RTLD_NOW | RTLD_LOCAL);
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
    vanished_thread_slots_are_reusable();
    nvml_backend();
    unavailable_is_safe();
    incomplete_nvml_is_not_shutdown_without_init();
}
