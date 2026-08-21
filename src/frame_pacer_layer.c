#define _GNU_SOURCE
#include "hud_fps.h"
#include "hud_metrics.h"
#include "hud_swapchain_policy.h"
#include "hud_text.h"
#include "hud_vertices.h"
#include "hud_vulkan_commands.h"
#include "hud_vulkan_draw_resources.h"
#include "hud_vulkan_device.h"
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

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
    struct frame_pacer_hud_vulkan_device hud;
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
    struct frame_pacer_hud_vertices vertices;
    bool draw_setup_attempted;
    bool disabled;
    bool submitted;
    /* This tracker lives beside the command buffer that draws the HUD.  Wine
     * may map an implicit layer into a separate linker namespace, so only
     * callback-local state is guaranteed to be shared with the visible draw. */
    struct frame_pacer_fps_tracker fps;
    struct hud_swapchain *next;
};

static struct instance *instances;
static struct physical_device *physical_devices;
static struct device *devices;
static struct queue *queues;
static struct hud_swapchain *hud_swapchains;
static PFN_vkGetInstanceProcAddr fallback_gipa;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct frame_pacer_runtime_log runtime_log =
    FRAME_PACER_RUNTIME_LOG_INITIALIZER(1024);
static uint64_t presents;
static struct frame_pacer_clock pacing_clock;
static struct frame_pacer_limit pacing_limit;
static struct frame_pacer_thread_cpu_quota thread_cpu_quota;
static pthread_once_t pacing_clock_once = PTHREAD_ONCE_INIT;
static pthread_once_t log_once = PTHREAD_ONCE_INIT;
static bool pacing_initialized;

#ifdef FRAME_PACER_TEST
void frame_pacer_layer_test_fail_next_allocation(void);
void frame_pacer_layer_test_fail_allocation_after(size_t);
uint32_t frame_pacer_layer_test_queue_family_count(VkPhysicalDevice);
uint32_t frame_pacer_layer_test_physical_device_count(void);
uint32_t frame_pacer_layer_test_queue_count(void);

static size_t allocations_before_failure = SIZE_MAX;

__attribute__((visibility("hidden")))
void frame_pacer_layer_test_fail_next_allocation(void)
{
    allocations_before_failure = 0;
}

__attribute__((visibility("hidden")))
void frame_pacer_layer_test_fail_allocation_after(size_t successes)
{
    allocations_before_failure = successes;
}
#endif

static void *allocate_zero(size_t count, size_t size)
{
#ifdef FRAME_PACER_TEST
    if (allocations_before_failure != SIZE_MAX) {
        if (!allocations_before_failure) {
            allocations_before_failure = SIZE_MAX;
            return 0;
        }
        --allocations_before_failure;
    }
#endif
    return calloc(count, size);
}

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
    va_list args;

    va_start(args, format);
    frame_pacer_runtime_log_vwrite(&runtime_log, format, args);
    va_end(args);
}

static void init_log_once(void)
{
    /* Wine can load a negotiated layer callback through a second linker
     * namespace in the same process.  Keep the single restrictive PID log so
     * both copies expose their lifecycle; O_APPEND keeps individual writes
     * intact and does not change rendering or pacing. */
    if (frame_pacer_runtime_log_open(&runtime_log, "frame-pacer-"))
        logmsg("frame-pacer: layer init pid=%ld architecture=%zu hud=enabled\n",
               (long)getpid(), sizeof(void *) * 8);
}

static void init_log(void)
{
    (void)pthread_once(&log_once, init_log_once);
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

#ifdef FRAME_PACER_TEST
__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_queue_family_count(VkPhysicalDevice physical)
{
    struct physical_device *item;
    uint32_t count;

    pthread_mutex_lock(&lock);
    item = find_physical_device(physical);
    count = item ? item->queue_family_count : 0;
    pthread_mutex_unlock(&lock);
    return count;
}

__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_physical_device_count(void)
{
    struct physical_device *item;
    uint32_t count = 0;

    pthread_mutex_lock(&lock);
    for (item = physical_devices; item; item = item->next)
        ++count;
    pthread_mutex_unlock(&lock);
    return count;
}

__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_queue_count(void)
{
    struct queue *item;
    uint32_t count = 0;

    pthread_mutex_lock(&lock);
    for (item = queues; item; item = item->next)
        ++count;
    pthread_mutex_unlock(&lock);
    return count;
}
#endif

static struct queue *find_queue(VkQueue queue)
{
    struct queue *item;

    for (item = queues; item; item = item->next)
        if (item->handle == queue)
            return item;
    return 0;
}

static struct hud_swapchain *find_hud_swapchain(VkSwapchainKHR swapchain)
{
    struct hud_swapchain *item;

    for (item = hud_swapchains; item; item = item->next)
        if (item->handle == swapchain)
            return item;
    return 0;
}

enum queue_collection_result {
    QUEUE_COLLECTION_READY,
    QUEUE_COLLECTION_UNAVAILABLE,
    QUEUE_COLLECTION_OUT_OF_MEMORY,
};

static void free_queue_list(struct queue *items)
{
    while (items) {
        struct queue *next = items->next;

        free(items);
        items = next;
    }
}

/* Queue handles are dispatchable objects.  Registering those created by the
 * game is what lets the loader route later queue commands through this layer,
 * even when the game never asks this layer directly for vkGetDeviceQueue. */
static enum queue_collection_result collect_declared_queues(
    struct device *device, const struct physical_device *physical,
    const VkDeviceCreateInfo *info, struct queue **result)
{
    struct queue *collected = 0;
    uint32_t create_index;
    PFN_vkGetDeviceQueue get_queue;

    if (!result)
        return QUEUE_COLLECTION_UNAVAILABLE;
    *result = 0;
    if (!device || !info ||
        (info->queueCreateInfoCount && !info->pQueueCreateInfos))
        return QUEUE_COLLECTION_UNAVAILABLE;
    if (!info->queueCreateInfoCount)
        return QUEUE_COLLECTION_READY;
    if (!device->gdpa || !device->set_loader_data)
        return QUEUE_COLLECTION_UNAVAILABLE;
    get_queue = (PFN_vkGetDeviceQueue)device->gdpa(device->handle, "vkGetDeviceQueue");
    if (!get_queue)
        return QUEUE_COLLECTION_UNAVAILABLE;

    for (create_index = 0; create_index < info->queueCreateInfoCount; ++create_index) {
        const VkDeviceQueueCreateInfo *create = &info->pQueueCreateInfos[create_index];
        uint32_t queue_index;

        for (queue_index = 0; queue_index < create->queueCount; ++queue_index) {
            VkQueue queue = VK_NULL_HANDLE;
            struct queue *item;

            get_queue(device->handle, create->queueFamilyIndex, queue_index, &queue);
            if (!queue || device->set_loader_data(device->handle, queue) != VK_SUCCESS) {
                logmsg("frame-pacer: queue loader-data registration failed "
                       "family=%u index=%u\n",
                       create->queueFamilyIndex, queue_index);
                free_queue_list(collected);
                return QUEUE_COLLECTION_UNAVAILABLE;
            }
            for (item = collected; item; item = item->next)
                if (item->handle == queue)
                    break;
            if (item)
                continue;
            item = allocate_zero(1, sizeof(*item));
            if (!item) {
                free_queue_list(collected);
                return QUEUE_COLLECTION_OUT_OF_MEMORY;
            }
            item->handle = queue;
            item->device = device;
            item->family = create->queueFamilyIndex;
            item->flags =
                physical && create->queueFamilyIndex < physical->queue_family_count
                    ? physical->queue_families[create->queueFamilyIndex].queueFlags
                    : 0;
            item->next = collected;
            collected = item;
        }
    }
    *result = collected;
    return QUEUE_COLLECTION_READY;
}
static void destroy_hud_swapchain(struct hud_swapchain *item)
{
    if (!item)
        return;
    frame_pacer_hud_destroy_pipeline(&item->pipeline, &item->device->hud.pipeline,
                                     item->device->handle, 0);
    frame_pacer_hud_destroy_vertex_buffer(&item->vertex_buffer,
        &item->device->hud.vertex_buffer, item->device->handle, 0);
    frame_pacer_hud_destroy_draw_resources(&item->draw_resources,
        &item->device->hud.draw, item->device->handle, 0);
    frame_pacer_hud_destroy_image_views(&item->image_views, &item->device->hud.resources,
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
    if (!device->hud.commands_ready) {
        logmsg("frame-pacer: HUD unavailable swapchain=%" PRIx64
               ": required Vulkan command missing; fail-open\n",
               (uint64_t)swapchain);
        return;
    }
    item = allocate_zero(1, sizeof(*item));
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
        &device->hud.resources, device->handle, swapchain, info->imageFormat,
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
            &item->device->hud.draw, item->device->handle, &item->image_views,
            item->format, item->extent, queue_state->family)) {
        logmsg("frame-pacer: HUD draw resources unavailable swapchain=%" PRIx64
               "; fail-open\n",
               (uint64_t)item->handle);
    } else {
        if (!frame_pacer_hud_create_pipeline(&item->pipeline, &item->device->hud.pipeline,
                item->device->handle, item->draw_resources.render_pass,
                (const uint32_t *)build_shaders_hud_vert_spv, sizeof(build_shaders_hud_vert_spv),
                (const uint32_t *)build_shaders_hud_frag_spv, sizeof(build_shaders_hud_frag_spv)))
            logmsg("frame-pacer: HUD pipeline unavailable swapchain=%" PRIx64
                   "; fail-open\n",
                   (uint64_t)item->handle);
        else if (!item->device->hud.has_memory_properties ||
                 !frame_pacer_hud_create_vertex_buffer(&item->vertex_buffer,
                    &item->device->hud.vertex_buffer, item->device->handle,
                    &item->device->hud.memory_properties,
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
    struct frame_pacer_hud_text text;
    struct frame_pacer_metrics_snapshot metrics;
    uint64_t now = monotonic(0);
    uint32_t fps = 0;
    bool fps_valid;
    if (!item || image_index >= item->draw_resources.count ||
        !item->vertex_buffer.map)
        return false;
    frame_pacer_hud_vulkan_device_metrics_snapshot(&item->device->hud, now,
                                                    &metrics);
    fps_valid = frame_pacer_fps_snapshot(&item->fps, &fps);
    {
        bool thread_cpu_quota_configured;
        uint32_t limit = current_limit(now);
        uint32_t thread_cpu_quota_percent = frame_pacer_limit_thread_cpu_quota(
            &pacing_limit, &thread_cpu_quota_configured);

        frame_pacer_hud_text_format(&text, &metrics,
                                    fps_valid, fps, limit,
                                    thread_cpu_quota_configured,
                                    frame_pacer_thread_cpu_quota_confirmed(&thread_cpu_quota, 0),
                                    thread_cpu_quota_percent);
    }
    if (!frame_pacer_hud_vertices_build(&item->vertices, &text) ||
        sizeof(item->vertices.data[0]) * (size_t)item->vertices.count >
            item->vertex_buffer.size)
        return false;
    memcpy(item->vertex_buffer.map, item->vertices.data,
           sizeof(item->vertices.data[0]) * (size_t)item->vertices.count);
    return frame_pacer_hud_record(&item->device->hud.record,
        item->draw_resources.command_buffers[image_index], item->image_views.images[image_index],
        item->draw_resources.framebuffers[image_index], item->draw_resources.render_pass,
        &item->pipeline, &item->vertex_buffer, item->extent,
        item->vertices.count);
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
    if (!frame_pacer_hud_prepare_present(&item->device->hud.present,
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

    {
        uint32_t capacity = count;

        handles = allocate_zero(capacity, sizeof(*handles));
        if (!handles)
            return;
        get_queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)instance->gipa(
            instance->handle, "vkGetPhysicalDeviceQueueFamilyProperties");
        if (enumerate(instance->handle, &count, handles) != VK_SUCCESS) {
            free(handles);
            return;
        }
        if (count > capacity)
            count = capacity;
    }

    for (index = 0; index < count; ++index) {
        struct physical_device *item = allocate_zero(1, sizeof(*item));

        if (item) {
            item->handle = handles[index];
            item->instance = instance;
            if (get_queues) {
                get_queues(handles[index], &item->queue_family_count, 0);
                if (item->queue_family_count) {
                    uint32_t capacity = item->queue_family_count;
                    uint32_t returned = capacity;

                    item->queue_families = allocate_zero(
                        capacity, sizeof(*item->queue_families));
                    if (item->queue_families) {
                        get_queues(handles[index], &returned,
                                   item->queue_families);
                        item->queue_family_count =
                            returned < capacity ? returned : capacity;
                    } else {
                        item->queue_family_count = 0;
                    }
                }
            }
            item->next = physical_devices;
            physical_devices = item;
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
    if (!info || !instance) {
        logmsg("frame-pacer: create instance missing create info or output\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    link = instance_create_info(info->pNext, VK_LAYER_LINK_INFO);
    if (!link || !link->u.pLayerInfo) {
        logmsg("frame-pacer: create instance missing link\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    if (!gipa) {
        logmsg("frame-pacer: create instance missing downstream resolver\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    create = (PFN_vkCreateInstance)gipa(0, "vkCreateInstance");
    result = create ? create(info, allocator, instance)
                    : VK_ERROR_INITIALIZATION_FAILED;
    logmsg("frame-pacer: create instance forwarded result=%d instance=%p\n", result,
           result == VK_SUCCESS && instance ? (void *)*instance : 0);
    if (result != VK_SUCCESS)
        return result;
    if (!instance || !*instance) {
        logmsg("frame-pacer: downstream create instance returned no handle\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    item = allocate_zero(1, sizeof(*item));
    if (!item) {
        PFN_vkDestroyInstance destroy =
            (PFN_vkDestroyInstance)gipa(*instance, "vkDestroyInstance");

        if (destroy)
            destroy(*instance, allocator);
        *instance = VK_NULL_HANDLE;
        logmsg("frame-pacer: create instance bookkeeping allocation failed\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
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
    struct queue *declared_queues;
    enum queue_collection_result queue_result;
    VkResult result;

    logmsg("frame-pacer: create device entered\n");
    if (!info || !device) {
        logmsg("frame-pacer: create device missing create info or output; "
               "initialization failed\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    link = device_create_info(info->pNext, VK_LAYER_LINK_INFO);
    data = device_create_info(info->pNext, VK_LOADER_DATA_CALLBACK);
    if (!link || !link->u.pLayerInfo) {
        logmsg("frame-pacer: create device missing loader link; "
               "initialization failed\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    if (!gdpa) {
        logmsg("frame-pacer: create device missing downstream resolver; "
               "initialization failed\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
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
    if (!device || !*device) {
        logmsg("frame-pacer: downstream create device returned no handle\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* VkDevice is already known to the loader.  Only queue objects acquired
     * below need pfnSetDeviceLoaderData.  Calling it for the device itself was
     * an invalid deviation from the standard layer model used by MangoHud. */
    item = allocate_zero(1, sizeof(*item));
    if (!item) {
        PFN_vkDestroyDevice destroy =
            (PFN_vkDestroyDevice)gdpa(*device, "vkDestroyDevice");

        if (destroy)
            destroy(*device, allocator);
        *device = VK_NULL_HANDLE;
        logmsg("frame-pacer: create device bookkeeping allocation failed\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
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
    frame_pacer_hud_vulkan_device_init(
        &item->hud, *device, physical, gdpa,
        instance ? instance->handle : VK_NULL_HANDLE,
        instance ? instance->gipa : 0, (unsigned int)getpid());
    queue_result = collect_declared_queues(item, physical_state, info,
                                           &declared_queues);
    if (queue_result != QUEUE_COLLECTION_READY) {
        frame_pacer_hud_vulkan_device_destroy(&item->hud);
        if (item->destroy_device)
            item->destroy_device(*device, allocator);
        free(item);
        *device = VK_NULL_HANDLE;
        logmsg("frame-pacer: create device queue registration failed\n");
        return queue_result == QUEUE_COLLECTION_OUT_OF_MEMORY
                   ? VK_ERROR_OUT_OF_HOST_MEMORY
                   : VK_ERROR_INITIALIZATION_FAILED;
    }

    pthread_mutex_lock(&lock);
    item->next = devices;
    devices = item;
    if (declared_queues) {
        struct queue *tail = declared_queues;

        while (tail->next)
            tail = tail->next;
        tail->next = queues;
        queues = declared_queues;
    }
    pthread_mutex_unlock(&lock);
    logmsg("frame-pacer: create device present=%s submit=%s surface-caps=%s "
           "hud-commands=%s\n",
           item->present ? "yes" : "no", item->submit ? "yes" : "no",
           item->hud.get_surface_capabilities ? "yes" : "no",
           item->hud.commands_ready ? "ready" : "missing");
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    struct instance **instance_link;
    struct physical_device **physical_link;
    struct instance *item;
    PFN_vkDestroyInstance destroy;

    pthread_mutex_lock(&lock);
    item = find_instance(instance);
    destroy = item && item->gipa
                  ? (PFN_vkDestroyInstance)item->gipa(instance,
                                                       "vkDestroyInstance")
                  : fallback_gipa
                        ? (PFN_vkDestroyInstance)fallback_gipa(
                              instance, "vkDestroyInstance")
                        : 0;
    physical_link = &physical_devices;
    while (*physical_link) {
        if ((*physical_link)->instance == item) {
            struct physical_device *found = *physical_link;

            *physical_link = found->next;
            free(found->queue_families);
            free(found);
        } else {
            physical_link = &(*physical_link)->next;
        }
    }
    instance_link = &instances;
    while (*instance_link && *instance_link != item)
        instance_link = &(*instance_link)->next;
    if (*instance_link)
        *instance_link = item->next;
    fallback_gipa = instances ? instances->gipa : 0;
    pthread_mutex_unlock(&lock);

    free(item);
    if (destroy)
        destroy(instance, allocator);
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
    outcome = frame_pacer_hud_create_swapchain(item->hud.get_surface_capabilities,
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
        frame_pacer_hud_vulkan_device_destroy(&item->hud);
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
        uint64_t accepted_ns = monotonic(0);
        uint32_t swapchain_index;

        pthread_mutex_lock(&lock);
        item = find_queue(queue);
        if (item) {
            resumed = item->pacer.fallback_active;
            item->pacer.last_present_ns = accepted_ns;
            frame_pacer_queue_note_present(&item->pacer);
        }
        if (info && info->pSwapchains)
            for (swapchain_index = 0; swapchain_index < info->swapchainCount;
                 ++swapchain_index) {
                struct hud_swapchain *hud =
                    find_hud_swapchain(info->pSwapchains[swapchain_index]);

                if (hud)
                    (void)frame_pacer_fps_record_present(&hud->fps, accepted_ns, 0);
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
    PFN_vkGetDeviceProcAddr gdpa;

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
    gdpa = item ? item->gdpa : 0;
    pthread_mutex_unlock(&lock);
    return gdpa ? gdpa(device, name) : 0;
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
    if (!strcmp(name, "vkDestroyInstance"))
        return (PFN_vkVoidFunction)vkDestroyInstance;
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
           __atomic_load_n(&presents, __ATOMIC_RELAXED),
           frame_pacer_runtime_log_bytes(&runtime_log));
    frame_pacer_runtime_log_close(&runtime_log);
    if (pacing_initialized) {
        frame_pacer_thread_cpu_quota_destroy(&thread_cpu_quota);
        frame_pacer_limit_destroy(&pacing_limit);
        frame_pacer_clock_destroy(&pacing_clock);
    }
}
