#ifndef FRAME_PACER_VULKAN_LAYER_HUD_H
#define FRAME_PACER_VULKAN_LAYER_HUD_H

#include "hud_fps.h"
#include "hud_text.h"
#include "vulkan_layer_registry.h"

#include <vulkan/vulkan.h>

#include <stdint.h>

struct frame_pacer_vulkan_hud_swapchain;

struct frame_pacer_vulkan_hud {
    struct frame_pacer_vulkan_registry *registry;
    struct frame_pacer_vulkan_hud_swapchain *swapchains;
    void *callback_context;
    uint64_t (*now)(void *context);
    void (*format_text)(void *context, struct frame_pacer_vulkan_device *device,
                        struct frame_pacer_fps_tracker *fps, uint64_t now,
                        struct frame_pacer_hud_text *text);
    void (*log)(const char *, ...);
};

#define FRAME_PACER_VULKAN_HUD_INITIALIZER(                                    \
    registry_value, context_value, now_callback, text_callback, logger)        \
    {.registry = (registry_value),                                             \
     .callback_context = (context_value),                                      \
     .now = (now_callback),                                                    \
     .format_text = (text_callback),                                           \
     .log = (logger)}

FRAME_PACER_VULKAN_INTERNAL void
frame_pacer_vulkan_hud_create_swapchain_resources(
    struct frame_pacer_vulkan_hud *hud,
    struct frame_pacer_vulkan_device *device, VkSwapchainKHR swapchain,
    const VkSwapchainCreateInfoKHR *info);
FRAME_PACER_VULKAN_INTERNAL void
frame_pacer_vulkan_hud_create_draw_resources(struct frame_pacer_vulkan_hud *hud,
                                             VkQueue queue,
                                             const VkPresentInfoKHR *info);
FRAME_PACER_VULKAN_INTERNAL const VkPresentInfoKHR *
frame_pacer_vulkan_hud_prepare_present(struct frame_pacer_vulkan_hud *hud,
                                       VkQueue queue,
                                       const VkPresentInfoKHR *info,
                                       VkPresentInfoKHR *replacement);
FRAME_PACER_VULKAN_INTERNAL void
frame_pacer_vulkan_hud_note_present(struct frame_pacer_vulkan_hud *hud,
                                    const VkPresentInfoKHR *info,
                                    uint64_t accepted_ns);
/* note_present and take_*_locked require hud->registry->lock.  Destruction is
 * deliberately performed after unlocking so Vulkan callbacks cannot reenter
 * the registry transaction. */
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_hud_swapchain *
frame_pacer_vulkan_hud_take_swapchain_locked(struct frame_pacer_vulkan_hud *hud,
                                             VkSwapchainKHR swapchain);
FRAME_PACER_VULKAN_INTERNAL struct frame_pacer_vulkan_hud_swapchain *
frame_pacer_vulkan_hud_take_device_swapchains_locked(
    struct frame_pacer_vulkan_hud *hud,
    struct frame_pacer_vulkan_device *device);
FRAME_PACER_VULKAN_INTERNAL void frame_pacer_vulkan_hud_destroy_swapchain_list(
    struct frame_pacer_vulkan_hud_swapchain *items);

#endif
