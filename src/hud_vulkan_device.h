#ifndef FRAME_PACER_HUD_VULKAN_DEVICE_H
#define FRAME_PACER_HUD_VULKAN_DEVICE_H

#include "hud_metrics_cache.h"
#include "hud_swapchain_policy.h"
#include "hud_vulkan_commands.h"
#include "hud_vulkan_draw_resources.h"
#include "hud_vulkan_pipeline.h"
#include "hud_vulkan_present.h"
#include "hud_vulkan_record.h"
#include "hud_vulkan_resources.h"
#include "hud_vulkan_vertex_buffer.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/* Owns every HUD facility resolved from one VkDevice.  The layer owns the
 * VkDevice itself; this object owns only provider tables, metrics state, and
 * the mutex used to publish a coherent metrics snapshot to render callbacks. */
struct frame_pacer_hud_vulkan_device {
    frame_pacer_hud_surface_capabilities_fn get_surface_capabilities;
    struct frame_pacer_hud_commands commands;
    struct frame_pacer_hud_vulkan_provider resources;
    struct frame_pacer_hud_draw_provider draw;
    struct frame_pacer_hud_pipeline_provider pipeline;
    struct frame_pacer_hud_vertex_buffer_provider vertex_buffer;
    struct frame_pacer_hud_record_provider record;
    struct frame_pacer_hud_present_provider present;
    VkPhysicalDeviceMemoryProperties memory_properties;
    bool has_memory_properties;
    bool commands_ready;
    struct frame_pacer_hud_metrics_cache metrics;
};

__attribute__((visibility("hidden")))
void frame_pacer_hud_vulkan_device_init(
    struct frame_pacer_hud_vulkan_device *, VkDevice, VkPhysicalDevice,
    PFN_vkGetDeviceProcAddr, VkInstance, PFN_vkGetInstanceProcAddr,
    unsigned int process_id);
__attribute__((visibility("hidden")))
void frame_pacer_hud_vulkan_device_destroy(
    struct frame_pacer_hud_vulkan_device *);
__attribute__((visibility("hidden")))
void frame_pacer_hud_vulkan_device_metrics_snapshot(
    struct frame_pacer_hud_vulkan_device *, uint64_t now_ns,
    struct frame_pacer_metrics_snapshot *);

#endif
