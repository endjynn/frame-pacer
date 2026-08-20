#define _GNU_SOURCE
#include "hud_fps.h"
#include "hud_metrics.h"
#include "hud_swapchain_policy.h"
#include "hud_text.h"
#include "hud_vertices.h"
#include "hud_vulkan_commands.h"
#include "hud_vulkan_draw_resources.h"
#include "hud_vulkan_pipeline.h"
#include "hud_vulkan_present.h"
#include "hud_vulkan_record.h"
#include "hud_vulkan_resources.h"
#include "hud_vulkan_vertex_buffer.h"
#include "hud_spv.h"
#include "log_retention.h"
#include "pacer_clock.h"
#include "pacer_limit.h"
#include "pacer_compatibility.h"
#include "pacer_queue.h"
#include "thread_cpu_quota.h"

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_LIMIT (UINT64_C(64) * 1024 * 1024)

struct instance {
    VkInstance handle;
    PFN_vkGetInstanceProcAddr gipa;
    struct instance *next;
};

struct physical_device {
    VkPhysicalDevice handle;
    struct instance *instance;
    VkQueueFamilyProperties *queue_families;
    uint32_t queue_family_count;
    struct physical_device *next;
};

struct device {
    VkDevice handle;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkSetDeviceLoaderData set_loader_data;
    PFN_vkQueuePresentKHR present;
    PFN_vkQueueSubmit submit;
    PFN_vkQueueSubmit2 submit2;
    PFN_vkQueueSubmit2KHR submit2_khr;
    PFN_vkCreateSwapchainKHR create_swapchain;
    PFN_vkDestroySwapchainKHR destroy_swapchain;
    PFN_vkDestroyDevice destroy_device;
    VkPhysicalDevice physical_device;
    frame_pacer_hud_surface_capabilities_fn get_surface_capabilities;
    struct frame_pacer_hud_vulkan_provider hud_provider;
    struct frame_pacer_hud_commands hud_commands;
    struct frame_pacer_hud_draw_provider hud_draw_provider;
    struct frame_pacer_hud_pipeline_provider hud_pipeline_provider;
    struct frame_pacer_hud_vertex_buffer_provider hud_vertex_buffer_provider;
    struct frame_pacer_hud_record_provider hud_record_provider;
    struct frame_pacer_hud_present_provider hud_present_provider;
    VkPhysicalDeviceMemoryProperties memory_properties;
    bool has_memory_properties;
    bool hud_commands_ready;
    struct frame_pacer_metrics metrics;
    struct frame_pacer_metrics_snapshot metrics_snapshot;
    uint64_t metrics_sample_ns;
    struct device *next;
};

struct queue {
    VkQueue handle;
    struct device *device;
    uint32_t family;
    VkQueueFlags flags;
    struct frame_pacer_queue_state pacer;
    struct queue *next;
};

struct hud_swapchain {
    VkSwapchainKHR handle;
    struct device *device;
    struct frame_pacer_hud_image_views image_views;
    VkExtent2D extent;
    VkFormat format;
    struct frame_pacer_hud_draw_resources draw_resources;
    struct frame_pacer_hud_pipeline pipeline;
    struct frame_pacer_hud_vertex_buffer vertex_buffer;
    bool draw_setup_attempted;
    bool disabled;
    bool submitted;
    /* This tracker lives beside the command buffer that draws the HUD.  Wine
     * may map an implicit layer into a separate linker namespace, so only
     * callback-local state is guaranteed to be shared with the visible draw. */
    struct frame_pacer_fps_tracker fps;
    bool fps_valid;
    uint32_t fps_value;
    struct hud_swapchain *next;
};

static struct instance *instances;
static struct physical_device *physical_devices;
static struct device *devices;
static struct queue *queues;
static struct hud_swapchain *hud_swapchains;
static PFN_vkGetInstanceProcAddr fallback_gipa;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int logfd = -1;
static uint64_t logbytes;
static uint64_t presents;
static uint64_t submit_fallbacks;
static bool capped;
static struct frame_pacer_clock pacing_clock;
static struct frame_pacer_limit pacing_limit;
static struct frame_pacer_thread_cpu_quota thread_cpu_quota;
static pthread_once_t pacing_clock_once = PTHREAD_ONCE_INIT;
static bool pacing_initialized;

static void logmsg(const char *format, ...);

static void thread_cpu_quota_log(const char *message)
{
    logmsg("%s", message);
}

static uint64_t monotonic(void *unused)
{
    struct timespec time;

    (void)unused;
    return clock_gettime(CLOCK_MONOTONIC, &time) ? 0 :
        (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static int sleep_until(void *unused, uint64_t deadline)
{
    struct timespec time = {
        .tv_sec = (time_t)(deadline / UINT64_C(1000000000)),
        .tv_nsec = (long)(deadline % UINT64_C(1000000000)),
    };
    (void)unused;
    return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, 0);
}

static void init_pacing_clock(void)
{
    frame_pacer_clock_init(&pacing_clock);
    frame_pacer_limit_init(&pacing_limit);
    frame_pacer_thread_cpu_quota_init(&thread_cpu_quota);
    frame_pacer_thread_cpu_quota_set_logger(&thread_cpu_quota, thread_cpu_quota_log);
    pacing_initialized = true;
}

/* The HUD's private submit path uses the next-layer function pointer directly,
 * so only game presents/submits reach this common pacing clock. */
static void pace(const char *path)
{
    struct frame_pacer_decision decision;
    bool changed;
    uint32_t fps;
    (void)pthread_once(&pacing_clock_once, init_pacing_clock);
    fps = frame_pacer_limit_poll(&pacing_limit, monotonic(0), &changed);
    {
        bool enabled;
        uint32_t percent = frame_pacer_limit_thread_cpu_quota(&pacing_limit, &enabled);
        frame_pacer_thread_cpu_quota_publish(&thread_cpu_quota, enabled, percent);
    }
    if (changed) logmsg("frame-pacer: Vulkan FPS limit changed to %u\n", fps);
    frame_pacer_clock_wait(&pacing_clock, fps, monotonic, sleep_until, 0, &decision);
    logmsg("frame-pacer: Vulkan %s first=%d missed=%d now=%" PRIu64
           " deadline=%" PRIu64 " eintr=%u cap=%u\n", path, decision.first,
           decision.missed, decision.observed_ns, decision.deadline_ns,
           decision.interruptions, fps);
}

static uint32_t current_limit(uint64_t now)
{
    (void)pthread_once(&pacing_clock_once, init_pacing_clock);
    return frame_pacer_limit_poll(&pacing_limit, now, 0);
}

static bool hud_enabled(uint64_t now)
{
    (void)current_limit(now);
    return frame_pacer_limit_hud_enabled(&pacing_limit);
}

static void logmsg(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    int length;
    size_t bytes;
    ssize_t written;

    if (logfd < 0 || capped)
        return;
    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0)
        return;
    bytes = (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1;
    if (logbytes + bytes + 48 > LOG_LIMIT) {
        ssize_t ignored = write(logfd, "frame-pacer: log cap reached; pacing continues\n", 47);

        (void)ignored;
        capped = true;
        return;
    }
    written = write(logfd, buffer, bytes);
    if (written > 0)
        logbytes += (uint64_t)written;
}

static void init_log(void)
{
    const char *state = getenv("XDG_STATE_HOME");
    const char *home;
    char root[1024];
    char directory[1100];
    char path[1200];

    if (!frame_pacer_log_enabled())
        return;
    if (!state || !*state) {
        home = getenv("HOME");
        if (!home || !*home)
            return;
        if (snprintf(root, sizeof(root), "%s/.local/state", home) >=
            (int)sizeof(root))
            return;
        if (mkdir(root, 0700) && errno != EEXIST)
            return;
        state = root;
    }
    if (snprintf(directory, sizeof(directory), "%s/frame-pacer", state) >=
        (int)sizeof(directory))
        return;
    if (mkdir(directory, 0700) && errno != EEXIST)
        return;
    if (snprintf(path, sizeof(path), "%s/frame-pacer-%ld.log", directory,
                 (long)getpid()) >= (int)sizeof(path))
        return;
    logfd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    /* Wine can load a negotiated layer callback through a second linker
     * namespace in the same process.  Keep the single restrictive PID log so
     * both copies expose their lifecycle; O_APPEND keeps individual writes
     * intact and does not change rendering or pacing. */
    if (logfd < 0 && errno == EEXIST)
        logfd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (logfd >= 0) {
        frame_pacer_log_retention_prune(directory, "frame-pacer-");
        logmsg("frame-pacer: layer init pid=%ld architecture=%zu hud=enabled\n",
               (long)getpid(), sizeof(void *) * 8);
    }
}

static struct device *find_device(VkDevice device)
{
    struct device *item;

    for (item = devices; item; item = item->next)
        if (item->handle == device)
            return item;
    return 0;
}

static struct instance *find_instance(VkInstance instance)
{
    struct instance *item;

    for (item = instances; item; item = item->next)
        if (item->handle == instance)
            return item;
    return 0;
}

static struct physical_device *find_physical_device(VkPhysicalDevice physical)
{
    struct physical_device *item;

    for (item = physical_devices; item; item = item->next)
        if (item->handle == physical)
            return item;
    return 0;
}

static struct queue *find_queue(VkQueue queue)
{
    struct queue *item;

    for (item = queues; item; item = item->next)
        if (item->handle == queue)
            return item;
    return 0;
}

static void remember_queue(VkQueue queue, struct device *device, uint32_t family,
                           VkQueueFlags flags)
{
    struct queue *item;

    if (!queue || !device || find_queue(queue))
        return;
    item = calloc(1, sizeof(*item));
    if (!item)
        return;
    item->handle = queue;
    item->device = device;
    item->family = family;
    item->flags = flags;
    item->next = queues;
    queues = item;
}
/* Queue handles are dispatchable objects.  Registering those created by the
 * game is what lets the loader route later queue commands through this layer,
 * even when the game never asks this layer directly for vkGetDeviceQueue. */
static void remember_declared_queues(struct device *device, const VkDeviceCreateInfo *info)
{
    uint32_t create_index, queue_index;
    PFN_vkGetDeviceQueue get_queue;
    if (!device || !info || !device->gdpa || !device->set_loader_data) return;
    get_queue = (PFN_vkGetDeviceQueue)device->gdpa(device->handle, "vkGetDeviceQueue");
    if (!get_queue) return;
    for (create_index = 0; create_index < info->queueCreateInfoCount; ++create_index) {
        const VkDeviceQueueCreateInfo *create = &info->pQueueCreateInfos[create_index];
        for (queue_index = 0; queue_index < create->queueCount; ++queue_index) {
            VkQueue queue = VK_NULL_HANDLE;
            get_queue(device->handle, create->queueFamilyIndex, queue_index, &queue);
            if (!queue || device->set_loader_data(device->handle, queue) != VK_SUCCESS) {
                logmsg("frame-pacer: queue loader-data registration failed "
                       "family=%u index=%u; fail-open\n",
                       create->queueFamilyIndex, queue_index);
                continue;
            }
            pthread_mutex_lock(&lock);
            {
                struct physical_device *physical = find_physical_device(device->physical_device);
                VkQueueFlags flags =
                    physical &&
                            create->queueFamilyIndex < physical->queue_family_count
                        ? physical->queue_families[create->queueFamilyIndex]
                              .queueFlags
                        : 0;
                remember_queue(queue, device, create->queueFamilyIndex, flags);
            }
            pthread_mutex_unlock(&lock);
        }
    }
}
static void destroy_hud_swapchain(struct hud_swapchain *item)
{
    if (!item)
        return;
    frame_pacer_hud_destroy_pipeline(&item->pipeline, &item->device->hud_pipeline_provider,
                                     item->device->handle, 0);
    frame_pacer_hud_destroy_vertex_buffer(&item->vertex_buffer,
        &item->device->hud_vertex_buffer_provider, item->device->handle, 0);
    frame_pacer_hud_destroy_draw_resources(&item->draw_resources,
        &item->device->hud_draw_provider, item->device->handle, 0);
    frame_pacer_hud_destroy_image_views(&item->image_views, &item->device->hud_provider,
                                        item->device->handle, 0);
    frame_pacer_fps_destroy(&item->fps);
    free(item);
}
static struct hud_swapchain *take_hud_swapchain(VkSwapchainKHR swapchain)
{
    struct hud_swapchain **link = &hud_swapchains;
    while (*link) {
        if ((*link)->handle == swapchain) {
            struct hud_swapchain *found = *link;
            *link = found->next;
            return found;
        }
        link = &(*link)->next;
    }
    return 0;
}
static struct hud_swapchain *take_device_hud_swapchains(struct device *device)
{
    struct hud_swapchain **link = &hud_swapchains;
    struct hud_swapchain *result = 0;
    while (*link) {
        if ((*link)->device == device) {
            struct hud_swapchain *found = *link;
            *link = found->next;
            found->next = result;
            result = found;
        } else {
            link = &(*link)->next;
        }
    }
    return result;
}
static void destroy_hud_swapchain_list(struct hud_swapchain *items)
{
    while (items) {
        struct hud_swapchain *next = items->next;
        destroy_hud_swapchain(items);
        items = next;
    }
}
static void hud_try_create_swapchain_resources(struct device *device,
                                               VkSwapchainKHR swapchain,
                                               const VkSwapchainCreateInfoKHR *info)
{
    struct hud_swapchain *item;
    enum frame_pacer_hud_resource_status status;
    if (!device || !swapchain || !info) return;
    if (!device->hud_commands_ready) {
        logmsg("frame-pacer: HUD unavailable swapchain=%" PRIx64
               ": required Vulkan command missing; fail-open\n",
               (uint64_t)swapchain);
        return;
    }
    item = calloc(1, sizeof(*item));
    if (!item) {
        logmsg("frame-pacer: HUD unavailable swapchain=%" PRIx64
               ": state allocation failed; fail-open\n",
               (uint64_t)swapchain);
        return;
    }
    item->handle = swapchain;
    item->device = device;
    item->extent = info->imageExtent;
    item->format = info->imageFormat;
    frame_pacer_fps_init(&item->fps);
    status = frame_pacer_hud_create_image_views(&item->image_views,
        &device->hud_provider, device->handle, swapchain, info->imageFormat,
        info->imageUsage);
    if (status != FRAME_PACER_HUD_RESOURCE_READY) {
        logmsg("frame-pacer: HUD unavailable swapchain=%" PRIx64 ": %s; fail-open\n",
               (uint64_t)swapchain, frame_pacer_hud_resource_status_string(status));
        destroy_hud_swapchain(item);
        return;
    }
    pthread_mutex_lock(&lock);
    item->next = hud_swapchains;
    hud_swapchains = item;
    pthread_mutex_unlock(&lock);
    logmsg("frame-pacer: HUD image resources ready swapchain=%" PRIx64 " images=%u\n",
           (uint64_t)swapchain, item->image_views.count);
}
static void hud_try_create_draw_resources(VkQueue queue, const VkPresentInfoKHR *info)
{
    struct queue *queue_state;
    struct hud_swapchain *item;
    if (!info || info->swapchainCount != 1 || !info->pSwapchains) return;
    pthread_mutex_lock(&lock);
    queue_state = find_queue(queue);
    item = take_hud_swapchain(info->pSwapchains[0]);
    if (item) { item->next = hud_swapchains; hud_swapchains = item; }
    if (!item || item->draw_resources.ready || item->draw_setup_attempted || !queue_state ||
        queue_state->device != item->device || !(queue_state->flags & VK_QUEUE_GRAPHICS_BIT)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    item->draw_setup_attempted = true;
    if (!frame_pacer_hud_create_draw_resources(&item->draw_resources,
            &item->device->hud_draw_provider, item->device->handle, &item->image_views,
            item->format, item->extent, queue_state->family)) {
        logmsg("frame-pacer: HUD draw resources unavailable swapchain=%" PRIx64
               "; fail-open\n",
               (uint64_t)item->handle);
    } else {
        if (!frame_pacer_hud_create_pipeline(&item->pipeline, &item->device->hud_pipeline_provider,
                item->device->handle, item->draw_resources.render_pass,
                (const uint32_t *)build_shaders_hud_vert_spv, sizeof(build_shaders_hud_vert_spv),
                (const uint32_t *)build_shaders_hud_frag_spv, sizeof(build_shaders_hud_frag_spv)))
            logmsg("frame-pacer: HUD pipeline unavailable swapchain=%" PRIx64
                   "; fail-open\n",
                   (uint64_t)item->handle);
        else if (!item->device->has_memory_properties ||
                 !frame_pacer_hud_create_vertex_buffer(&item->vertex_buffer,
                    &item->device->hud_vertex_buffer_provider, item->device->handle,
                    &item->device->memory_properties,
                    sizeof(struct frame_pacer_hud_vertices)))
            logmsg("frame-pacer: HUD vertex buffer unavailable swapchain=%" PRIx64
                   "; fail-open\n",
                   (uint64_t)item->handle);
        else
            logmsg("frame-pacer: HUD command resources ready swapchain=%" PRIx64
                   " images=%u family=%u vertex-bytes=%zu\n",
                   (uint64_t)item->handle, item->draw_resources.count,
                   queue_state->family,
                   sizeof(struct frame_pacer_hud_vertices));
    }
    pthread_mutex_unlock(&lock);
}
static bool hud_record_overlay(void *context, uint32_t image_index)
{
    struct hud_swapchain *item = context;
    struct frame_pacer_hud_vertices vertices;
    struct frame_pacer_hud_text text;
    uint64_t now = monotonic(0);
    if (!item || image_index >= item->draw_resources.count ||
        !item->vertex_buffer.map)
        return false;
    if (!item->device->metrics_sample_ns ||
        now - item->device->metrics_sample_ns >= FRAME_PACER_FPS_SAMPLE_NS) {
        frame_pacer_metrics_sample(&item->device->metrics, &item->device->metrics_snapshot);
        item->device->metrics_sample_ns = now;
    }
    /* Count the cadence of successfully recorded overlay frames.  This runs
     * in exactly the layer namespace that owns the visible swapchain. */
    {
        uint32_t sampled_fps = 0;
        if (frame_pacer_fps_record_present(&item->fps, now, &sampled_fps)) {
            item->fps_value = sampled_fps;
            item->fps_valid = sampled_fps != 0;
        }
    }
    {
        bool thread_cpu_quota_configured;
        uint32_t limit = current_limit(now);
        uint32_t thread_cpu_quota_percent = frame_pacer_limit_thread_cpu_quota(
            &pacing_limit, &thread_cpu_quota_configured);

        frame_pacer_hud_text_format(&text, &item->device->metrics_snapshot,
                                    item->fps_valid, item->fps_value, limit,
                                    thread_cpu_quota_configured,
                                    frame_pacer_thread_cpu_quota_confirmed(&thread_cpu_quota, 0),
                                    thread_cpu_quota_percent);
    }
    if (!frame_pacer_hud_vertices_build(&vertices, &text) ||
        sizeof(vertices.data[0]) * (size_t)vertices.count > item->vertex_buffer.size)
        return false;
    memcpy(item->vertex_buffer.map, vertices.data,
           sizeof(vertices.data[0]) * (size_t)vertices.count);
    return frame_pacer_hud_record(&item->device->hud_record_provider,
        item->draw_resources.command_buffers[image_index], item->image_views.images[image_index],
        item->draw_resources.framebuffers[image_index], item->draw_resources.render_pass,
        &item->pipeline, &item->vertex_buffer, item->extent, vertices.count);
}
static const VkPresentInfoKHR *hud_try_present(VkQueue queue,
    const VkPresentInfoKHR *info, VkPresentInfoKHR *replacement)
{
    struct queue *queue_state;
    struct hud_swapchain *item;
    uint32_t image_index;
    if (!info || !replacement || info->swapchainCount != 1 || !info->pSwapchains ||
        !info->pImageIndices)
        return info;
    pthread_mutex_lock(&lock);
    queue_state = find_queue(queue);
    item = take_hud_swapchain(info->pSwapchains[0]);
    if (item) { item->next = hud_swapchains; hud_swapchains = item; }
    pthread_mutex_unlock(&lock);
    if (!item || item->disabled || !queue_state || queue_state->device != item->device ||
        !(queue_state->flags & VK_QUEUE_GRAPHICS_BIT) || !item->draw_resources.ready ||
        !item->pipeline.pipeline || !item->vertex_buffer.buffer ||
        info->pImageIndices[0] >= item->draw_resources.count)
        return info;
    image_index = info->pImageIndices[0];
    if (!frame_pacer_hud_prepare_present(&item->device->hud_present_provider,
            item->device->handle, queue, info, item->draw_resources.fences[image_index],
            &item->draw_resources.semaphores[image_index],
            item->draw_resources.command_buffers[image_index], image_index,
            item->draw_resources.count, hud_record_overlay, item, replacement)) {
        item->disabled = true;
        logmsg("frame-pacer: HUD overlay submission unavailable swapchain=%" PRIx64 "; fail-open\n",
               (uint64_t)item->handle);
        return info;
    }
    if (!item->submitted) {
        item->submitted = true;
        logmsg("frame-pacer: HUD overlay submitted swapchain=%" PRIx64 " image=%u\n",
               (uint64_t)item->handle, image_index);
    }
    return replacement;
}
static void remember_physical_devices(struct instance *instance)
{
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queues;
    VkPhysicalDevice *handles;
    uint32_t count = 0;
    uint32_t index;

    enumerate = (PFN_vkEnumeratePhysicalDevices)instance->gipa(
        instance->handle, "vkEnumeratePhysicalDevices");
    if (!enumerate || enumerate(instance->handle, &count, 0) != VK_SUCCESS ||
        !count)
        return;

    handles = calloc(count, sizeof(*handles));
    if (!handles)
        return;
    get_queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)instance->gipa(
        instance->handle, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (enumerate(instance->handle, &count, handles) == VK_SUCCESS) {
        for (index = 0; index < count; ++index) {
        struct physical_device *item = calloc(1, sizeof(*item));

        if (item) {
            item->handle = handles[index];
            item->instance = instance;
            if (get_queues) {
                get_queues(handles[index], &item->queue_family_count, 0);
                if (item->queue_family_count) {
                    item->queue_families = calloc(
                        item->queue_family_count, sizeof(*item->queue_families));
                    if (item->queue_families)
                        get_queues(handles[index], &item->queue_family_count,
                                   item->queue_families);
                    else
                        item->queue_family_count = 0;
                }
            }
            item->next = physical_devices;
            physical_devices = item;
        }
        }
    }
    free(handles);
}

static VkLayerInstanceCreateInfo *instance_create_info(const void *next,
                                                       VkLayerFunction function)
{
    VkLayerInstanceCreateInfo *item = (VkLayerInstanceCreateInfo *)next;

    while (item) {
        if (item->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            item->function == function)
            return item;
        item = (VkLayerInstanceCreateInfo *)item->pNext;
    }
    return 0;
}

static VkLayerDeviceCreateInfo *device_create_info(const void *next,
                                                   VkLayerFunction function)
{
    VkLayerDeviceCreateInfo *item = (VkLayerDeviceCreateInfo *)next;

    while (item) {
        if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            item->function == function)
            return item;
        item = (VkLayerDeviceCreateInfo *)item->pNext;
    }
    return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *info,
                 const VkAllocationCallbacks *allocator, VkInstance *instance)
{
    VkLayerInstanceCreateInfo *link;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create;
    struct instance *item;
    VkResult result;

    init_log();
    logmsg("frame-pacer: create instance entered info=%p out=%p\n",
           (const void *)info, (void *)instance);
    if (!info) {
        logmsg("frame-pacer: create instance missing create info\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    link = instance_create_info(info->pNext, VK_LAYER_LINK_INFO);
    if (!link || !link->u.pLayerInfo) {
        logmsg("frame-pacer: create instance missing link\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    create = (PFN_vkCreateInstance)gipa(0, "vkCreateInstance");
    result = create ? create(info, allocator, instance)
                    : VK_ERROR_INITIALIZATION_FAILED;
    logmsg("frame-pacer: create instance forwarded result=%d instance=%p\n", result,
           result == VK_SUCCESS && instance ? (void *)*instance : 0);
    if (result != VK_SUCCESS)
        return result;

    item = calloc(1, sizeof(*item));
    if (!item)
        return VK_SUCCESS;
    item->handle = *instance;
    item->gipa = gipa;

    pthread_mutex_lock(&lock);
    fallback_gipa = gipa;
    item->next = instances;
    instances = item;
    remember_physical_devices(item);
    pthread_mutex_unlock(&lock);
    logmsg("frame-pacer: create instance\n");
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
               const VkAllocationCallbacks *allocator, VkDevice *device)
{
    VkLayerDeviceCreateInfo *link;
    VkLayerDeviceCreateInfo *data;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateDevice create;
    struct physical_device *physical_state;
    struct instance *instance;
    struct device *item;
    VkResult result;

    logmsg("frame-pacer: create device entered\n");
    if (!info) {
        logmsg("frame-pacer: create device missing create info; fail-open\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    link = device_create_info(info->pNext, VK_LAYER_LINK_INFO);
    data = device_create_info(info->pNext, VK_LOADER_DATA_CALLBACK);
    if (!link || !link->u.pLayerInfo) {
        logmsg("frame-pacer: create device missing link; fail-open\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    pthread_mutex_lock(&lock);
    physical_state = find_physical_device(physical);
    instance = physical_state ? physical_state->instance : 0;
    pthread_mutex_unlock(&lock);
    create = instance ? (PFN_vkCreateDevice)instance->gipa(instance->handle,
                                                           "vkCreateDevice")
                      : 0;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    result = create ? create(physical, info, allocator, device)
                    : VK_ERROR_INITIALIZATION_FAILED;
    if (result != VK_SUCCESS)
        return result;

    /* VkDevice is already known to the loader.  Only queue objects acquired
     * below need pfnSetDeviceLoaderData.  Calling it for the device itself was
     * an invalid deviation from the standard layer model used by MangoHud. */
    item = calloc(1, sizeof(*item));
    if (!item)
        return VK_SUCCESS;
    item->handle = *device;
    item->gdpa = gdpa;
    item->set_loader_data = data ? data->u.pfnSetDeviceLoaderData : 0;
    item->present = (PFN_vkQueuePresentKHR)gdpa(*device, "vkQueuePresentKHR");
    item->submit = (PFN_vkQueueSubmit)gdpa(*device, "vkQueueSubmit");
    item->submit2 = (PFN_vkQueueSubmit2)gdpa(*device, "vkQueueSubmit2");
    item->submit2_khr =
        (PFN_vkQueueSubmit2KHR)gdpa(*device, "vkQueueSubmit2KHR");
    item->create_swapchain =
        (PFN_vkCreateSwapchainKHR)gdpa(*device, "vkCreateSwapchainKHR");
    item->destroy_swapchain =
        (PFN_vkDestroySwapchainKHR)gdpa(*device, "vkDestroySwapchainKHR");
    item->destroy_device =
        (PFN_vkDestroyDevice)gdpa(*device, "vkDestroyDevice");
    item->physical_device = physical;
    item->get_surface_capabilities = instance ?
        (frame_pacer_hud_surface_capabilities_fn)instance->gipa(
            instance->handle, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") : 0;
    if (instance) {
        PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties =
            (PFN_vkGetPhysicalDeviceMemoryProperties)instance->gipa(instance->handle,
                "vkGetPhysicalDeviceMemoryProperties");
        if (get_memory_properties) {
            get_memory_properties(physical, &item->memory_properties);
            item->has_memory_properties = true;
        }
    }
    item->hud_provider.get_swapchain_images =
        (PFN_vkGetSwapchainImagesKHR)gdpa(*device, "vkGetSwapchainImagesKHR");
    item->hud_provider.create_image_view =
        (PFN_vkCreateImageView)gdpa(*device, "vkCreateImageView");
    item->hud_provider.destroy_image_view =
        (PFN_vkDestroyImageView)gdpa(*device, "vkDestroyImageView");
    item->hud_commands_ready =
        frame_pacer_hud_resolve_commands(&item->hud_commands, gdpa, *device);
    frame_pacer_metrics_init(&item->metrics, 0, (unsigned int)getpid());
    item->hud_draw_provider = (struct frame_pacer_hud_draw_provider){
        .create_render_pass = (PFN_vkCreateRenderPass)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_RENDER_PASS],
        .destroy_render_pass = (PFN_vkDestroyRenderPass)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_RENDER_PASS],
        .create_framebuffer = (PFN_vkCreateFramebuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_FRAMEBUFFER],
        .destroy_framebuffer = (PFN_vkDestroyFramebuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_FRAMEBUFFER],
        .create_command_pool = (PFN_vkCreateCommandPool)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_COMMAND_POOL],
        .destroy_command_pool = (PFN_vkDestroyCommandPool)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_COMMAND_POOL],
        .allocate_command_buffers = (PFN_vkAllocateCommandBuffers)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_ALLOCATE_COMMAND_BUFFERS],
        .create_fence = (PFN_vkCreateFence)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_FENCE],
        .destroy_fence = (PFN_vkDestroyFence)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_FENCE],
        .create_semaphore = (PFN_vkCreateSemaphore)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_SEMAPHORE],
        .destroy_semaphore = (PFN_vkDestroySemaphore)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_SEMAPHORE],
    };
    item->hud_pipeline_provider = (struct frame_pacer_hud_pipeline_provider){
        .create_shader_module = (PFN_vkCreateShaderModule)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_SHADER_MODULE],
        .destroy_shader_module = (PFN_vkDestroyShaderModule)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_SHADER_MODULE],
        .create_pipeline_layout = (PFN_vkCreatePipelineLayout)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_PIPELINE_LAYOUT],
        .destroy_pipeline_layout = (PFN_vkDestroyPipelineLayout)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE_LAYOUT],
        .create_graphics_pipelines = (PFN_vkCreateGraphicsPipelines)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_GRAPHICS_PIPELINES],
        .destroy_pipeline = (PFN_vkDestroyPipeline)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE],
    };
    item->hud_vertex_buffer_provider = (struct frame_pacer_hud_vertex_buffer_provider){
        .create_buffer = (PFN_vkCreateBuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_BUFFER],
        .destroy_buffer = (PFN_vkDestroyBuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DESTROY_BUFFER],
        .allocate_memory = (PFN_vkAllocateMemory)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_ALLOCATE_MEMORY],
        .free_memory = (PFN_vkFreeMemory)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_FREE_MEMORY],
        .map_memory = (PFN_vkMapMemory)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_MAP_MEMORY],
        .unmap_memory = (PFN_vkUnmapMemory)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_UNMAP_MEMORY],
        .bind_memory = (PFN_vkBindBufferMemory)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_BIND_BUFFER_MEMORY],
        .get_requirements = (PFN_vkGetBufferMemoryRequirements)
            item->hud_commands.functions[
                FRAME_PACER_HUD_COMMAND_GET_BUFFER_MEMORY_REQUIREMENTS],
    };
    item->hud_record_provider = (struct frame_pacer_hud_record_provider){
        .reset_command_buffer = (PFN_vkResetCommandBuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_RESET_COMMAND_BUFFER],
        .begin_command_buffer = (PFN_vkBeginCommandBuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_BEGIN_COMMAND_BUFFER],
        .end_command_buffer = (PFN_vkEndCommandBuffer)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_END_COMMAND_BUFFER],
        .pipeline_barrier = (PFN_vkCmdPipelineBarrier)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_PIPELINE_BARRIER],
        .begin_render_pass = (PFN_vkCmdBeginRenderPass)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_BEGIN_RENDER_PASS],
        .end_render_pass = (PFN_vkCmdEndRenderPass)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_END_RENDER_PASS],
        .bind_pipeline = (PFN_vkCmdBindPipeline)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_BIND_PIPELINE],
        .bind_vertex_buffers = (PFN_vkCmdBindVertexBuffers)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_BIND_VERTEX_BUFFERS],
        .draw = (PFN_vkCmdDraw)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_DRAW],
        .set_viewport = (PFN_vkCmdSetViewport)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_SET_VIEWPORT],
        .set_scissor = (PFN_vkCmdSetScissor)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_SET_SCISSOR],
        .push_constants = (PFN_vkCmdPushConstants)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_PUSH_CONSTANTS],
    };
    item->hud_present_provider = (struct frame_pacer_hud_present_provider){
        .wait_for_fences = (PFN_vkWaitForFences)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_WAIT_FOR_FENCES],
        .reset_fences = (PFN_vkResetFences)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_RESET_FENCES],
        .queue_submit = (PFN_vkQueueSubmit)
            item->hud_commands.functions[FRAME_PACER_HUD_COMMAND_QUEUE_SUBMIT],
    };

    pthread_mutex_lock(&lock);
    item->next = devices;
    devices = item;
    pthread_mutex_unlock(&lock);
    if (!item->set_loader_data)
        logmsg("frame-pacer: create device missing queue loader-data callback; HUD unavailable\n");
    else
        remember_declared_queues(item, info);
    logmsg("frame-pacer: create device present=%s submit=%s surface-caps=%s "
           "hud-commands=%s\n",
           item->present ? "yes" : "no", item->submit ? "yes" : "no",
           item->get_surface_capabilities ? "yes" : "no",
           item->hud_commands_ready ? "ready" : "missing");
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,
    const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator,
    VkSwapchainKHR *out)
{
    struct device *item;
    PFN_vkCreateSwapchainKHR create;
    struct frame_pacer_hud_swapchain_result outcome;
    pthread_mutex_lock(&lock);
    item = find_device(device);
    create = item ? item->create_swapchain : 0;
    pthread_mutex_unlock(&lock);
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    outcome = frame_pacer_hud_create_swapchain(item->get_surface_capabilities,
        create, item->physical_device, device, info, allocator, out);
    if (outcome.retried_original)
        logmsg("frame-pacer: HUD augmented swapchain request failed; original request retried\n");
    if (outcome.result == VK_SUCCESS && out && *out) {
        VkSwapchainCreateInfoKHR effective = *info;
        if (outcome.color_attachment_enabled)
            effective.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        hud_try_create_swapchain_resources(item, *out, &effective);
    }
    return outcome.result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device,
    VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator)
{
    struct device *item;
    struct hud_swapchain *hud;
    PFN_vkDestroySwapchainKHR destroy;
    pthread_mutex_lock(&lock);
    item = find_device(device);
    destroy = item ? item->destroy_swapchain : 0;
    hud = take_hud_swapchain(swapchain);
    pthread_mutex_unlock(&lock);
    destroy_hud_swapchain(hud);
    if (destroy) destroy(device, swapchain, allocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,
    const VkAllocationCallbacks *allocator)
{
    struct device **device_link;
    struct queue **queue_link;
    struct device *item;
    struct hud_swapchain *hud;
    PFN_vkDestroyDevice destroy;
    pthread_mutex_lock(&lock);
    item = find_device(device);
    destroy = item ? item->destroy_device : 0;
    hud = item ? take_device_hud_swapchains(item) : 0;
    device_link = &devices;
    while (*device_link && *device_link != item) device_link = &(*device_link)->next;
    if (*device_link) *device_link = item->next;
    queue_link = &queues;
    while (*queue_link) {
        if ((*queue_link)->device == item) {
            struct queue *found = *queue_link;
            *queue_link = found->next;
            free(found);
        } else {
            queue_link = &(*queue_link)->next;
        }
    }
    pthread_mutex_unlock(&lock);
    destroy_hud_swapchain_list(hud);
    if (item) {
        frame_pacer_metrics_destroy(&item->metrics);
        free(item);
    }
    if (destroy)
        destroy(device, allocator);
}
static void pace_submit_fallback(VkQueue queue, uint32_t submit_count,
                                 const char *path)
{
    struct queue *item;
    bool entered = false, needed = false;
    uint64_t queue_submits = 0;
    uint64_t now = monotonic(0);

    (void)pthread_once(&pacing_clock_once, init_pacing_clock);
    if (frame_pacer_compatibility_quiet_submit_policy(
            frame_pacer_limit_executable(&pacing_limit)) ==
        FRAME_PACER_QUIET_SUBMIT_FORWARD)
        return;

    pthread_mutex_lock(&lock);
    item = find_queue(queue);
    if (item)
        needed = frame_pacer_queue_needs_fallback(&item->pacer, now, &entered,
                                                   &queue_submits);
    pthread_mutex_unlock(&lock);
    if (!needed)
        return;

    pace(path);
    (void)__atomic_add_fetch(&submit_fallbacks, 1, __ATOMIC_RELAXED);
    if (entered)
        logmsg("frame-pacer: Vulkan submit fallback entered; presentation quiet\n");
    logmsg("frame-pacer: Vulkan %s fallback count=%u queue_submits=%" PRIu64 "\n",
           path, submit_count, queue_submits);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
    struct queue *item;
    struct device *device;
    VkPresentInfoKHR replacement;
    const VkPresentInfoKHR *forward = info;
    VkResult result;
    bool resumed = false;

    pthread_mutex_lock(&lock);
    item = find_queue(queue);
    device = item ? item->device : 0;
    if (item && !(item->flags & VK_QUEUE_GRAPHICS_BIT))
        logmsg("frame-pacer: HUD unavailable present queue family=%u lacks "
               "graphics capability; fail-open\n",
               item->family);
    pthread_mutex_unlock(&lock);
    if (!device || !device->present)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (hud_enabled(monotonic(0)))
        hud_try_create_draw_resources(queue, info);
    if (!__atomic_load_n(&presents, __ATOMIC_RELAXED))
        logmsg("frame-pacer: HUD observed present\n");
    if (hud_enabled(monotonic(0)))
        forward = hud_try_present(queue, info, &replacement);
    /* Pace immediately before the game's real present.  The optional HUD
     * submission above is deliberately not an additional pacing decision. */
    pace("present");
    result = device->present(queue, forward);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        pthread_mutex_lock(&lock);
        item = find_queue(queue);
        if (item) {
            resumed = item->pacer.fallback_active;
            item->pacer.last_present_ns = monotonic(0);
            frame_pacer_queue_note_present(&item->pacer);
        }
        pthread_mutex_unlock(&lock);
        if (resumed)
            logmsg("frame-pacer: submit fallback ended; present resumed\n");
    }
    (void)__atomic_add_fetch(&presents, 1, __ATOMIC_RELAXED);
    logmsg("frame-pacer: present result=%d\n", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo *infos,
              VkFence fence)
{
    struct queue *item;
    PFN_vkQueueSubmit submit;

    pace_submit_fallback(queue, count, "submit");
    pthread_mutex_lock(&lock);
    item = find_queue(queue);
    submit = item ? item->device->submit : 0;
    pthread_mutex_unlock(&lock);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *infos,
               VkFence fence)
{
    struct queue *item;
    PFN_vkQueueSubmit2 submit;

    pace_submit_fallback(queue, count, "submit2");
    pthread_mutex_lock(&lock);
    item = find_queue(queue);
    submit = item ? item->device->submit2 : 0;
    pthread_mutex_unlock(&lock);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2 *infos,
                  VkFence fence)
{
    struct queue *item;
    PFN_vkQueueSubmit2KHR submit;

    pace_submit_fallback(queue, count, "submit2KHR");
    pthread_mutex_lock(&lock);
    item = find_queue(queue);
    submit = item ? item->device->submit2_khr : 0;
    pthread_mutex_unlock(&lock);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    struct device *item;

    if (!name)
        return 0;
    if (!strcmp(name, "vkGetDeviceProcAddr")) {
        logmsg("frame-pacer: HUD gdpa vkGetDeviceProcAddr\n");
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (!strcmp(name, "vkQueuePresentKHR")) {
        logmsg("frame-pacer: HUD gdpa vkQueuePresentKHR\n");
        return (PFN_vkVoidFunction)vkQueuePresentKHR;
    }
    if (!strcmp(name, "vkQueueSubmit"))
        return (PFN_vkVoidFunction)vkQueueSubmit;
    if (!strcmp(name, "vkQueueSubmit2"))
        return (PFN_vkVoidFunction)vkQueueSubmit2;
    if (!strcmp(name, "vkQueueSubmit2KHR"))
        return (PFN_vkVoidFunction)vkQueueSubmit2KHR;
    if (!strcmp(name, "vkCreateSwapchainKHR"))
        return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if (!strcmp(name, "vkDestroySwapchainKHR"))
        return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)vkDestroyDevice;

    pthread_mutex_lock(&lock);
    item = find_device(device);
    pthread_mutex_unlock(&lock);
    return item && item->gdpa ? item->gdpa(device, name) : 0;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    struct instance *item;
    PFN_vkGetInstanceProcAddr gipa;

    if (!name)
        return 0;
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) {
        logmsg("frame-pacer: gipa vkCreateInstance instance=%p\n",
               (void *)instance);
        return (PFN_vkVoidFunction)vkCreateInstance;
    }
    if (!strcmp(name, "vkCreateDevice")) {
        logmsg("frame-pacer: HUD gipa vkCreateDevice\n");
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (!strcmp(name, "vkQueuePresentKHR")) {
        logmsg("frame-pacer: HUD gipa vkQueuePresentKHR\n");
        return (PFN_vkVoidFunction)vkQueuePresentKHR;
    }
    if (!strcmp(name, "vkQueueSubmit"))
        return (PFN_vkVoidFunction)vkQueueSubmit;
    if (!strcmp(name, "vkQueueSubmit2"))
        return (PFN_vkVoidFunction)vkQueueSubmit2;
    if (!strcmp(name, "vkQueueSubmit2KHR"))
        return (PFN_vkVoidFunction)vkQueueSubmit2KHR;
    if (!strcmp(name, "vkCreateSwapchainKHR"))
        return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if (!strcmp(name, "vkDestroySwapchainKHR"))
        return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)vkDestroyDevice;

    pthread_mutex_lock(&lock);
    item = find_instance(instance);
    gipa = item && item->gipa ? item->gipa : fallback_gipa;
    pthread_mutex_unlock(&lock);
    return gipa ? gipa(instance, name) : 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version)
{
    init_log();
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
        version->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (version->loaderLayerInterfaceVersion > 2)
        version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = 0;
    logmsg("frame-pacer: layer negotiated interface=%u\n",
           version->loaderLayerInterfaceVersion);
    return VK_SUCCESS;
}

static void __attribute__((destructor)) frame_pacer_shutdown(void)
{
    logmsg("frame-pacer: shutdown presents=%" PRIu64 " log_bytes=%" PRIu64 "\n",
           __atomic_load_n(&presents, __ATOMIC_RELAXED), logbytes);
    if (logfd >= 0)
        (void)close(logfd);
    if (pacing_initialized) {
        frame_pacer_thread_cpu_quota_destroy(&thread_cpu_quota);
        frame_pacer_limit_destroy(&pacing_limit);
        frame_pacer_clock_destroy(&pacing_clock);
    }
}
