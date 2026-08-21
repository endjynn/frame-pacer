#ifndef FRAME_PACER_VULKAN_LAYER_REGISTRY_H
#define FRAME_PACER_VULKAN_LAYER_REGISTRY_H

#include "hud_vulkan_device.h"
#include "pacer_queue.h"

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_PACER_VULKAN_INTERNAL __attribute__((visibility("hidden")))

struct frame_pacer_vulkan_instance {
    VkInstance handle;
    PFN_vkGetInstanceProcAddr gipa;
    struct frame_pacer_vulkan_instance *next;
};

struct frame_pacer_vulkan_physical_device {
    VkPhysicalDevice handle;
    struct frame_pacer_vulkan_instance *instance;
    VkQueueFamilyProperties *queue_families;
    uint32_t queue_family_count;
    struct frame_pacer_vulkan_physical_device *next;
};

struct frame_pacer_vulkan_device {
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
    struct frame_pacer_vulkan_device *next;
};

struct frame_pacer_vulkan_queue {
    VkQueue handle;
    struct frame_pacer_vulkan_device *device;
    uint32_t family;
    VkQueueFlags flags;
    struct frame_pacer_queue_state pacer;
    struct frame_pacer_vulkan_queue *next;
};

struct frame_pacer_vulkan_registry {
    struct frame_pacer_vulkan_instance *instances;
    struct frame_pacer_vulkan_physical_device *physical_devices;
    struct frame_pacer_vulkan_device *devices;
    struct frame_pacer_vulkan_queue *queues;
    PFN_vkGetInstanceProcAddr fallback_gipa;
    pthread_mutex_t lock;
    void (*log)(const char *, ...);
};

#define FRAME_PACER_VULKAN_REGISTRY_INITIALIZER(logger) \
    { .lock = PTHREAD_MUTEX_INITIALIZER, .log = (logger) }

enum frame_pacer_vulkan_queue_collection_result {
    FRAME_PACER_VULKAN_QUEUES_READY,
    FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE,
    FRAME_PACER_VULKAN_QUEUES_OUT_OF_MEMORY,
};

/* find_* and remove_device_locked require registry->lock.  The remaining
 * mutating operations acquire it internally unless their name says locked. */
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_registry_lock(
    struct frame_pacer_vulkan_registry *registry);
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_registry_unlock(
    struct frame_pacer_vulkan_registry *registry);
FRAME_PACER_VULKAN_INTERNAL void *frame_pacer_vulkan_registry_allocate_zero(
    size_t count, size_t size);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_instance *
frame_pacer_vulkan_registry_find_instance(
    struct frame_pacer_vulkan_registry *registry, VkInstance instance);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_physical_device *
frame_pacer_vulkan_registry_find_physical_device(
    struct frame_pacer_vulkan_registry *registry, VkPhysicalDevice physical);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_device *
frame_pacer_vulkan_registry_find_device(
    struct frame_pacer_vulkan_registry *registry, VkDevice device);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_queue *
frame_pacer_vulkan_registry_find_queue(
    struct frame_pacer_vulkan_registry *registry, VkQueue queue);
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_registry_add_instance(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_instance *instance);
FRAME_PACER_VULKAN_INTERNAL PFN_vkDestroyInstance
frame_pacer_vulkan_registry_remove_instance(
    struct frame_pacer_vulkan_registry *registry, VkInstance instance,
    struct frame_pacer_vulkan_instance **removed);
FRAME_PACER_VULKAN_INTERNAL enum frame_pacer_vulkan_queue_collection_result
frame_pacer_vulkan_registry_collect_queues(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_device *device,
    const struct frame_pacer_vulkan_physical_device *physical,
    const VkDeviceCreateInfo *info,
    struct frame_pacer_vulkan_queue **result);
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_registry_add_device(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_device *device,
    struct frame_pacer_vulkan_queue *queues);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_device *
frame_pacer_vulkan_registry_remove_device_locked(
    struct frame_pacer_vulkan_registry *registry, VkDevice device);
#ifdef FRAME_PACER_TEST
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_registry_set_test_registry(
    struct frame_pacer_vulkan_registry *registry);
FRAME_PACER_VULKAN_INTERNAL void
frame_pacer_layer_test_fail_next_allocation(void);
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_layer_test_fail_allocation_after(
    size_t successes);
FRAME_PACER_VULKAN_INTERNAL uint32_t
frame_pacer_layer_test_queue_family_count(VkPhysicalDevice physical);
FRAME_PACER_VULKAN_INTERNAL uint32_t
frame_pacer_layer_test_physical_device_count(void);
FRAME_PACER_VULKAN_INTERNAL uint32_t frame_pacer_layer_test_queue_count(void);
#endif

#endif
