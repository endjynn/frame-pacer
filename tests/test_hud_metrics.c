#include "hud_metrics.h"
#include <assert.h>
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
    assert(frame_pacer_metrics_parse_cpu_stat("usage_usec 12345\n", "usage_usec", &total));
    assert(total == 12345);
    assert(!frame_pacer_metrics_parse_cpu_stat("throttled_usec 12345\n", "usage_usec", &total));
}

static void render_node_parser(void)
{
    char node[32];
    assert(frame_pacer_metrics_parse_render_node("/dev/dri/renderD128", node, sizeof(node)));
    assert(!strcmp(node, "renderD128"));
    assert(!frame_pacer_metrics_parse_render_node("/dev/dri/card1", node, sizeof(node)));
}

static void drm_fdinfo_parser(void)
{
    uint64_t value;
    unsigned int percent;
    assert(frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\t15514652816 ns\n", &value));
    assert(value == UINT64_C(15514652816));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-copy:\t1 ns\n", &value));
    assert(!frame_pacer_drm_fdinfo_parse_render_ns("drm-engine-render:\tbad ns\n", &value));
    assert(frame_pacer_drm_fdinfo_utilisation(100, 150, 200, &percent) && percent == 25);
    assert(frame_pacer_drm_fdinfo_utilisation(100, 500, 200, &percent) && percent == 100);
    assert(!frame_pacer_drm_fdinfo_utilisation(500, 100, 200, &percent));
}

static void nvml_backend(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    frame_pacer_metrics_init(&metrics, "build/test-nvml.so", (unsigned int)getpid());
    assert(!metrics.nvml_device);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(metrics.nvml_device);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_USE) && snapshot.gpu_use_percent == 37);
    assert((snapshot.available & FRAME_PACER_METRIC_GPU_TEMP) && snapshot.gpu_temp_celsius == 64);
    frame_pacer_metrics_destroy(&metrics);
}

static void unavailable_is_safe(void)
{
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot snapshot;
    frame_pacer_metrics_init(&metrics, "build/test-nvml.so", UINT32_MAX);
    assert(!metrics.nvml_device);
    frame_pacer_metrics_sample(&metrics, &snapshot);
    assert(!(snapshot.available & (FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP)));
    frame_pacer_metrics_destroy(&metrics);
}

int main(void)
{
    cpu_parser();
    render_node_parser();
    drm_fdinfo_parser();
    nvml_backend();
    unavailable_is_safe();
}
