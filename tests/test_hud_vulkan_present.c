#include "hud_vulkan_present.h"

#include <assert.h>
#include <stdint.h>

static unsigned int waits;
static unsigned int resets;
static unsigned int records;
static unsigned int submits;
static bool fail_submit;

static VkResult VKAPI_CALL wait_for_fences(VkDevice device, uint32_t count,
                                           const VkFence *fences,
                                           VkBool32 wait_all, uint64_t timeout)
{
    (void)device;
    (void)count;
    (void)fences;
    (void)wait_all;
    (void)timeout;
    ++waits;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL reset_fences(VkDevice device, uint32_t count,
                                        const VkFence *fences)
{
    (void)device;
    (void)count;
    (void)fences;
    ++resets;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL queue_submit(VkQueue queue, uint32_t count,
                                        const VkSubmitInfo *info, VkFence fence)
{
    (void)queue;
    (void)fence;
    assert(count == 1);
    assert(info->signalSemaphoreCount == 1);
    assert(info->commandBufferCount == 1);
    assert(info->pCommandBuffers);
    assert(info->pCommandBuffers[0] == (VkCommandBuffer)(uintptr_t)5);
    ++submits;
    return fail_submit ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static bool record(void *context, uint32_t image_index)
{
    (void)context;
    assert(image_index == 1);
    ++records;
    return true;
}

static const struct frame_pacer_hud_present_provider provider = {
    .wait_for_fences = wait_for_fences,
    .reset_fences = reset_fences,
    .queue_submit = queue_submit,
};

int main(void)
{
    VkSemaphore game_semaphore = (VkSemaphore)(uintptr_t)1;
    VkSemaphore hud_semaphore = (VkSemaphore)(uintptr_t)2;
    VkSwapchainKHR swapchain = (VkSwapchainKHR)(uintptr_t)3;
    uint32_t image_index = 1;
    VkPresentInfoKHR original = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &game_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };
    VkPresentInfoKHR replacement = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    };
    const VkDevice device = (VkDevice)(uintptr_t)1;
    const VkQueue queue = (VkQueue)(uintptr_t)2;
    const VkFence fence = (VkFence)(uintptr_t)4;
    const VkCommandBuffer command = (VkCommandBuffer)(uintptr_t)5;

    waits = resets = records = submits = 0;
    fail_submit = false;
    assert(frame_pacer_hud_prepare_present(
        &provider, device, queue, &original, fence, &hud_semaphore, command,
        image_index, 3, record, 0, &replacement));
    assert(waits == 1 && resets == 1 && records == 1 && submits == 1);
    assert(replacement.pWaitSemaphores == &hud_semaphore);
    assert(original.pWaitSemaphores == &game_semaphore);

    replacement = (VkPresentInfoKHR){
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    };
    fail_submit = true;
    assert(!frame_pacer_hud_prepare_present(
        &provider, device, queue, &original, fence, &hud_semaphore, command,
        image_index, 3, record, 0, &replacement));
    assert(replacement.sType == VK_STRUCTURE_TYPE_APPLICATION_INFO);

    assert(!frame_pacer_hud_prepare_present(
        &provider, device, queue, &original, fence, &hud_semaphore, command, 3,
        3, record, 0, &replacement));
    return 0;
}
