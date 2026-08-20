#ifndef FRAME_PACER_HUD_VULKAN_RESOURCES_H
#define FRAME_PACER_HUD_VULKAN_RESOURCES_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/* The renderer never asks the game to resize its swapchain.  Eight images is
 * deliberately conservative for the observed target and makes setup bounded. */
#define FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES 8u

struct frame_pacer_hud_vulkan_provider {
    PFN_vkGetSwapchainImagesKHR get_swapchain_images;
    PFN_vkCreateImageView create_image_view;
    PFN_vkDestroyImageView destroy_image_view;
};

struct frame_pacer_hud_image_views {
    VkImage images[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkImageView views[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    uint32_t count;
    bool ready;
};

enum frame_pacer_hud_resource_status {
    FRAME_PACER_HUD_RESOURCE_READY,
    FRAME_PACER_HUD_RESOURCE_MISSING_FUNCTION,
    FRAME_PACER_HUD_RESOURCE_NO_COLOR_ATTACHMENT,
    FRAME_PACER_HUD_RESOURCE_IMAGE_QUERY_FAILED,
    FRAME_PACER_HUD_RESOURCE_IMAGE_COUNT_INVALID,
    FRAME_PACER_HUD_RESOURCE_VIEW_CREATE_FAILED,
};

enum frame_pacer_hud_resource_status frame_pacer_hud_create_image_views(
    struct frame_pacer_hud_image_views *resources,
    const struct frame_pacer_hud_vulkan_provider *provider,
    VkDevice device, VkSwapchainKHR swapchain, VkFormat format,
    VkImageUsageFlags image_usage);

void frame_pacer_hud_destroy_image_views(
    struct frame_pacer_hud_image_views *resources,
    const struct frame_pacer_hud_vulkan_provider *provider,
    VkDevice device, const VkAllocationCallbacks *allocator);

const char *frame_pacer_hud_resource_status_string(
    enum frame_pacer_hud_resource_status status);

#endif
