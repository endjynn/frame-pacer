#include "hud_vulkan_present.h"
#include "hud_vulkan_resources.h"

bool frame_pacer_hud_prepare_present(const struct frame_pacer_hud_present_provider *p,
    VkDevice device, VkQueue queue, const VkPresentInfoKHR *original, VkFence fence,
    const VkSemaphore *signal, VkCommandBuffer command, uint32_t image_index, uint32_t image_count,
    frame_pacer_hud_record_fn record, void *context, VkPresentInfoKHR *replacement)
{
    VkPipelineStageFlags stages[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = original ? original->waitSemaphoreCount : 0,
        .pWaitSemaphores = original ? original->pWaitSemaphores : 0,
        .commandBufferCount = 1, .pCommandBuffers = &command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = signal};
    uint32_t wait_index;
    if (!p || !p->wait_for_fences || !p->reset_fences || !p->queue_submit ||
        !original || !fence || !signal || !*signal || !command || !record || !replacement ||
        image_index >= image_count || original->swapchainCount != 1 ||
        original->waitSemaphoreCount > FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES ||
        (original->waitSemaphoreCount && !original->pWaitSemaphores))
        return false;
    for (wait_index = 0; wait_index < original->waitSemaphoreCount; ++wait_index)
        stages[wait_index] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    submit.pWaitDstStageMask = original->waitSemaphoreCount ? stages : 0;
    if (p->wait_for_fences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS ||
        p->reset_fences(device, 1, &fence) != VK_SUCCESS || !record(context, image_index) ||
        p->queue_submit(queue, 1, &submit, fence) != VK_SUCCESS)
        return false;
    *replacement = *original;
    replacement->waitSemaphoreCount = 1;
    replacement->pWaitSemaphores = signal;
    return true;
}
