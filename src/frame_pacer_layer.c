#define _GNU_SOURCE
#include "hud_metrics.h"
#include "hud_swapchain_policy.h"
#include "hud_text.h"
#include "log_retention.h"
#include "pacer_clock.h"
#include "pacer_limit.h"
#include "pacer_compatibility.h"
#include "thread_cpu_quota.h"
#include "vulkan_layer_hud.h"
#include "vulkan_layer_registry.h"

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

static struct frame_pacer_runtime_log runtime_log =
    FRAME_PACER_RUNTIME_LOG_INITIALIZER(1024);
static uint64_t presents;
static struct frame_pacer_clock pacing_clock;
static struct frame_pacer_limit pacing_limit;
static struct frame_pacer_thread_cpu_quota thread_cpu_quota;
static pthread_once_t pacing_clock_once = PTHREAD_ONCE_INIT;
static pthread_once_t log_once = PTHREAD_ONCE_INIT;
static bool pacing_initialized;

static void logmsg(const char *format, ...);
static uint64_t monotonic(void *unused);
static void format_hud_text(void *context,
                            struct frame_pacer_vulkan_device *device,
                            struct frame_pacer_fps_tracker *fps_tracker,
                            uint64_t now, struct frame_pacer_hud_text *text);

static struct frame_pacer_vulkan_registry registry =
    FRAME_PACER_VULKAN_REGISTRY_INITIALIZER(logmsg);
static struct frame_pacer_vulkan_hud vulkan_hud =
    FRAME_PACER_VULKAN_HUD_INITIALIZER(&registry, 0, monotonic,
                                       format_hud_text, logmsg);

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

static void format_hud_text(void *context,
                            struct frame_pacer_vulkan_device *device,
                            struct frame_pacer_fps_tracker *fps_tracker,
                            uint64_t now, struct frame_pacer_hud_text *text)
{
    struct frame_pacer_metrics_snapshot metrics;
    bool fps_valid;
    bool thread_cpu_quota_configured;
    uint32_t fps = 0;
    uint32_t limit;
    uint32_t thread_cpu_quota_percent;

    (void)context;
    frame_pacer_hud_vulkan_device_metrics_snapshot(&device->hud, now,
                                                    &metrics);
    fps_valid = frame_pacer_fps_snapshot(fps_tracker, &fps);
    limit = current_limit(now);
    thread_cpu_quota_percent = frame_pacer_limit_thread_cpu_quota(
        &pacing_limit, &thread_cpu_quota_configured);
    frame_pacer_hud_text_format(
        text, &metrics, fps_valid, fps, limit, thread_cpu_quota_configured,
        frame_pacer_thread_cpu_quota_confirmed(&thread_cpu_quota, 0),
        thread_cpu_quota_percent);
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
#ifdef FRAME_PACER_TEST
    frame_pacer_vulkan_registry_set_test_registry(&registry);
#endif
    (void)pthread_once(&log_once, init_log_once);
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
    struct frame_pacer_vulkan_instance *item;
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

    item = frame_pacer_vulkan_registry_allocate_zero(1, sizeof(*item));
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
    item->destroy_instance =
        (PFN_vkDestroyInstance)gipa(*instance, "vkDestroyInstance");

    frame_pacer_vulkan_registry_add_instance(&registry, item);
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
    struct frame_pacer_vulkan_physical_device *physical_state;
    struct frame_pacer_vulkan_instance *instance;
    struct frame_pacer_vulkan_device *item;
    struct frame_pacer_vulkan_queue *declared_queues;
    enum frame_pacer_vulkan_queue_collection_result queue_result;
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
    frame_pacer_vulkan_registry_lock(&registry);
    physical_state =
        frame_pacer_vulkan_registry_find_physical_device(&registry, physical);
    instance = physical_state ? physical_state->instance : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
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
    item = frame_pacer_vulkan_registry_allocate_zero(1, sizeof(*item));
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
    queue_result = frame_pacer_vulkan_registry_collect_queues(
        &registry, item, physical_state, info, &declared_queues);
    if (queue_result != FRAME_PACER_VULKAN_QUEUES_READY) {
        frame_pacer_hud_vulkan_device_destroy(&item->hud);
        if (item->destroy_device)
            item->destroy_device(*device, allocator);
        free(item);
        *device = VK_NULL_HANDLE;
        logmsg("frame-pacer: create device queue registration failed\n");
        return queue_result == FRAME_PACER_VULKAN_QUEUES_OUT_OF_MEMORY
                   ? VK_ERROR_OUT_OF_HOST_MEMORY
                   : VK_ERROR_INITIALIZATION_FAILED;
    }

    frame_pacer_vulkan_registry_add_device(&registry, item, declared_queues);
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
    struct frame_pacer_vulkan_instance *item;
    PFN_vkDestroyInstance destroy;

    logmsg("frame-pacer: destroy instance entered instance=%p\n",
           (void *)instance);
    destroy = frame_pacer_vulkan_registry_remove_instance(
        &registry, instance, &item);
    logmsg("frame-pacer: destroy instance registry removed=%s downstream=%s\n",
           item ? "yes" : "no", destroy ? "yes" : "no");
    free(item);
    if (destroy) {
        destroy(instance, allocator);
        logmsg("frame-pacer: destroy instance forwarded\n");
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,
    const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator,
    VkSwapchainKHR *out)
{
    struct frame_pacer_vulkan_device *item;
    PFN_vkCreateSwapchainKHR create;
    struct frame_pacer_hud_swapchain_result outcome;
    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_device(&registry, device);
    create = item ? item->create_swapchain : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    outcome = frame_pacer_hud_create_swapchain(item->hud.get_surface_capabilities,
        create, item->physical_device, device, info, allocator, out);
    if (outcome.retried_original)
        logmsg("frame-pacer: HUD augmented swapchain request failed; original request retried\n");
    if (outcome.result == VK_SUCCESS && out && *out) {
        VkSwapchainCreateInfoKHR effective = *info;
        if (outcome.color_attachment_enabled)
            effective.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        frame_pacer_vulkan_hud_create_swapchain_resources(
            &vulkan_hud, item, *out, &effective);
    }
    return outcome.result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device,
    VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator)
{
    struct frame_pacer_vulkan_device *item;
    struct frame_pacer_vulkan_hud_swapchain *hud;
    PFN_vkDestroySwapchainKHR destroy;
    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_device(&registry, device);
    destroy = item ? item->destroy_swapchain : 0;
    hud = frame_pacer_vulkan_hud_take_swapchain_locked(&vulkan_hud,
                                                        swapchain);
    frame_pacer_vulkan_registry_unlock(&registry);
    frame_pacer_vulkan_hud_destroy_swapchain_list(hud);
    if (destroy) destroy(device, swapchain, allocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,
    const VkAllocationCallbacks *allocator)
{
    struct frame_pacer_vulkan_device *item;
    struct frame_pacer_vulkan_hud_swapchain *hud;
    PFN_vkDestroyDevice destroy;

    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_device(&registry, device);
    destroy = item ? item->destroy_device : 0;
    hud = item ? frame_pacer_vulkan_hud_take_device_swapchains_locked(
                     &vulkan_hud, item)
               : 0;
    item = frame_pacer_vulkan_registry_remove_device_locked(&registry, device);
    frame_pacer_vulkan_registry_unlock(&registry);
    frame_pacer_vulkan_hud_destroy_swapchain_list(hud);
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
    struct frame_pacer_vulkan_queue *item;
    bool entered = false, needed = false;
    uint64_t queue_submits = 0;
    uint64_t now = monotonic(0);

    (void)pthread_once(&pacing_clock_once, init_pacing_clock);
    if (frame_pacer_compatibility_quiet_submit_policy(
            frame_pacer_limit_executable(&pacing_limit)) ==
        FRAME_PACER_QUIET_SUBMIT_FORWARD)
        return;

    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
    if (item)
        needed = frame_pacer_queue_needs_fallback(&item->pacer, now, &entered,
                                                   &queue_submits);
    frame_pacer_vulkan_registry_unlock(&registry);
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
    struct frame_pacer_vulkan_queue *item;
    struct frame_pacer_vulkan_device *device;
    VkPresentInfoKHR replacement;
    const VkPresentInfoKHR *forward = info;
    VkResult result;
    bool resumed = false;

    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
    device = item ? item->device : 0;
    if (item && !(item->flags & VK_QUEUE_GRAPHICS_BIT))
        logmsg("frame-pacer: HUD unavailable present queue family=%u lacks "
               "graphics capability; fail-open\n",
               item->family);
    frame_pacer_vulkan_registry_unlock(&registry);
    if (!device || !device->present)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (hud_enabled(monotonic(0)))
        frame_pacer_vulkan_hud_create_draw_resources(&vulkan_hud, queue, info);
    if (!__atomic_load_n(&presents, __ATOMIC_RELAXED))
        logmsg("frame-pacer: HUD observed present\n");
    if (hud_enabled(monotonic(0)))
        forward = frame_pacer_vulkan_hud_prepare_present(
            &vulkan_hud, queue, info, &replacement);
    /* Pace immediately before the game's real present.  The optional HUD
     * submission above is deliberately not an additional pacing decision. */
    pace("present");
    result = device->present(queue, forward);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        uint64_t accepted_ns = monotonic(0);
        frame_pacer_vulkan_registry_lock(&registry);
        item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
        if (item) {
            resumed = item->pacer.fallback_active;
            item->pacer.last_present_ns = accepted_ns;
            frame_pacer_queue_note_present(&item->pacer);
        }
        frame_pacer_vulkan_hud_note_present(&vulkan_hud, info, accepted_ns);
        frame_pacer_vulkan_registry_unlock(&registry);
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
    struct frame_pacer_vulkan_queue *item;
    PFN_vkQueueSubmit submit;

    pace_submit_fallback(queue, count, "submit");
    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
    submit = item ? item->device->submit : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *infos,
               VkFence fence)
{
    struct frame_pacer_vulkan_queue *item;
    PFN_vkQueueSubmit2 submit;

    pace_submit_fallback(queue, count, "submit2");
    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
    submit = item ? item->device->submit2 : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2 *infos,
                  VkFence fence)
{
    struct frame_pacer_vulkan_queue *item;
    PFN_vkQueueSubmit2KHR submit;

    pace_submit_fallback(queue, count, "submit2KHR");
    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_queue(&registry, queue);
    submit = item ? item->device->submit2_khr : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
    return submit ? submit(queue, count, infos, fence)
                  : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    struct frame_pacer_vulkan_device *item;
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

    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_device(&registry, device);
    gdpa = item ? item->gdpa : 0;
    frame_pacer_vulkan_registry_unlock(&registry);
    return gdpa ? gdpa(device, name) : 0;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    struct frame_pacer_vulkan_instance *item;
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

    frame_pacer_vulkan_registry_lock(&registry);
    item = frame_pacer_vulkan_registry_find_instance(&registry, instance);
    gipa = item && item->gipa ? item->gipa : registry.fallback_gipa;
    frame_pacer_vulkan_registry_unlock(&registry);
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
