#ifndef FRAME_PACER_HUD_VULKAN_DRAW_RESOURCES_H
#define FRAME_PACER_HUD_VULKAN_DRAW_RESOURCES_H

#include "hud_vulkan_resources.h"

struct frame_pacer_hud_draw_provider {
    PFN_vkCreateRenderPass create_render_pass;
    PFN_vkDestroyRenderPass destroy_render_pass;
    PFN_vkCreateFramebuffer create_framebuffer;
    PFN_vkDestroyFramebuffer destroy_framebuffer;
    PFN_vkCreateCommandPool create_command_pool;
    PFN_vkDestroyCommandPool destroy_command_pool;
    PFN_vkAllocateCommandBuffers allocate_command_buffers;
    PFN_vkCreateFence create_fence;
    PFN_vkDestroyFence destroy_fence;
    PFN_vkCreateSemaphore create_semaphore;
    PFN_vkDestroySemaphore destroy_semaphore;
};

struct frame_pacer_hud_draw_resources {
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    VkFramebuffer framebuffers[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkCommandBuffer command_buffers[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkFence fences[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkSemaphore semaphores[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    uint32_t count;
    bool ready;
};

bool frame_pacer_hud_create_draw_resources(
    struct frame_pacer_hud_draw_resources *,
    const struct frame_pacer_hud_draw_provider *, VkDevice,
    const struct frame_pacer_hud_image_views *, VkFormat, VkExtent2D, uint32_t);
void frame_pacer_hud_destroy_draw_resources(
    struct frame_pacer_hud_draw_resources *,
    const struct frame_pacer_hud_draw_provider *, VkDevice,
    const VkAllocationCallbacks *);

#endif
