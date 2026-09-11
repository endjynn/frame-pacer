#ifndef FRAME_PACER_HUD_VULKAN_TEXTURE_H
#define FRAME_PACER_HUD_VULKAN_TEXTURE_H

#include "hud_font_atlas.h"
#include "hud_vulkan_commands.h"

/* Owned by one swapchain; upload uses its first overlay submission. Staging
 * storage stays alive until teardown so no extra queue wait is required. */
struct frame_pacer_hud_texture {
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSetLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet descriptor;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    const struct frame_pacer_font_atlas *atlas;
    VkDeviceSize allocation_bytes;
    bool uploaded;
};

__attribute__((visibility("hidden"))) bool frame_pacer_hud_create_texture(
    struct frame_pacer_hud_texture *, const struct frame_pacer_hud_commands *,
    VkDevice, const VkPhysicalDeviceMemoryProperties *,
    const struct frame_pacer_font_atlas *);
__attribute__((visibility("hidden"))) void
frame_pacer_hud_destroy_texture(struct frame_pacer_hud_texture *,
                                const struct frame_pacer_hud_commands *,
                                VkDevice);
/* Called inside a begun command buffer, outside a render pass. Mark uploaded
 * only after command recording succeeds; failed submissions disable the HUD. */
__attribute__((visibility("hidden"))) void
frame_pacer_hud_record_texture_upload(const struct frame_pacer_hud_texture *,
                                      const struct frame_pacer_hud_commands *,
                                      VkCommandBuffer);

#endif
