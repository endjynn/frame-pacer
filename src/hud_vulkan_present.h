#ifndef FRAME_PACER_HUD_VULKAN_PRESENT_H
#define FRAME_PACER_HUD_VULKAN_PRESENT_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef bool (*frame_pacer_hud_record_fn)(void *context, uint32_t image_index);
struct frame_pacer_hud_present_provider {
    PFN_vkWaitForFences wait_for_fences;
    PFN_vkResetFences reset_fences;
    PFN_vkQueueSubmit queue_submit;
};

/* On success, `replacement` is a stack-safe copy whose only changed field is
 * its wait list. On failure, it leaves `replacement` untouched. */
bool frame_pacer_hud_prepare_present(
    const struct frame_pacer_hud_present_provider *, VkDevice, VkQueue,
    const VkPresentInfoKHR *, VkFence, const VkSemaphore *, VkCommandBuffer,
    uint32_t image_index, uint32_t image_count, frame_pacer_hud_record_fn,
    void *, VkPresentInfoKHR *replacement);

#endif
