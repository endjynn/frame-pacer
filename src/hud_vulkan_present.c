#include "hud_vulkan_present.h"
#include "hud_vulkan_resources.h"

#include <stdlib.h>

bool frame_pacer_hud_prepare_present(const struct frame_pacer_hud_present_provider *p,
    VkDevice device, VkQueue queue, const VkPresentInfoKHR *original, VkFence fence,
    const VkSemaphore *signal, VkCommandBuffer command, uint32_t image_index, uint32_t image_count,
    frame_pacer_hud_record_fn record, void *context, VkPresentInfoKHR *replacement)
{
    VkPipelineStageFlags stack_stages[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
    VkPipelineStageFlags *stages = stack_stages;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = original ? original->waitSemaphoreCount : 0,
        .pWaitSemaphores = original ? original->pWaitSemaphores : 0,
        .commandBufferCount = 1, .pCommandBuffers = &command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = signal};
    uint32_t wait_index;
    bool prepared = false;
    if (!p || !p->wait_for_fences || !p->reset_fences || !p->queue_submit ||
        !original || !fence || !signal || !*signal || !command || !record || !replacement ||
        image_index >= image_count || original->swapchainCount != 1 ||
        (original->waitSemaphoreCount && !original->pWaitSemaphores))
        return false;
    if (original->waitSemaphoreCount > FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES) {
#if SIZE_MAX == UINT32_MAX
        if ((size_t)original->waitSemaphoreCount > SIZE_MAX / sizeof(*stages))
            return false;
#endif
        stages = malloc((size_t)original->waitSemaphoreCount * sizeof(*stages));
        if (!stages) return false;
    }
    for (wait_index = 0; wait_index < original->waitSemaphoreCount; ++wait_index)
        stages[wait_index] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    submit.pWaitDstStageMask = original->waitSemaphoreCount ? stages : 0;
    if (p->wait_for_fences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS ||
        p->reset_fences(device, 1, &fence) != VK_SUCCESS || !record(context, image_index) ||
        p->queue_submit(queue, 1, &submit, fence) != VK_SUCCESS)
        goto done;
    *replacement = *original;
    replacement->waitSemaphoreCount = 1;
    replacement->pWaitSemaphores = signal;
    prepared = true;
done:
    if (stages != stack_stages) free(stages);
    return prepared;
}
