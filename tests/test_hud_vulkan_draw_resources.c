#include "hud_vulkan_draw_resources.h"

#include <assert.h>
#include <stdint.h>

static unsigned int creates;
static unsigned int destroys;
static unsigned int fail_at;

static VkResult next_result(void)
{
    return ++creates == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}

static VkResult VKAPI_CALL create_render_pass(
    VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *render_pass)
{
    (void)device;
    (void)info;
    (void)allocator;
    *render_pass = (VkRenderPass)(uintptr_t)1;
    return next_result();
}

static VkResult VKAPI_CALL create_framebuffer(
    VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *framebuffer)
{
    (void)device;
    (void)info;
    (void)allocator;
    *framebuffer = (VkFramebuffer)(uintptr_t)(10 + creates);
    return next_result();
}

static VkResult VKAPI_CALL create_command_pool(
    VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *command_pool)
{
    (void)device;
    (void)info;
    (void)allocator;
    *command_pool = (VkCommandPool)(uintptr_t)2;
    return next_result();
}

static VkResult VKAPI_CALL allocate_command_buffers(
    VkDevice device, const VkCommandBufferAllocateInfo *info,
    VkCommandBuffer *command_buffers)
{
    uint32_t index;

    (void)device;
    for (index = 0; index < info->commandBufferCount; ++index)
        command_buffers[index] = (VkCommandBuffer)(uintptr_t)(20 + index);
    return next_result();
}

static VkResult VKAPI_CALL create_fence(
    VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *fence)
{
    (void)device;
    (void)info;
    (void)allocator;
    *fence = (VkFence)(uintptr_t)(30 + creates);
    return next_result();
}

static VkResult VKAPI_CALL create_semaphore(
    VkDevice device, const VkSemaphoreCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkSemaphore *semaphore)
{
    (void)device;
    (void)info;
    (void)allocator;
    *semaphore = (VkSemaphore)(uintptr_t)(40 + creates);
    return next_result();
}

static void VKAPI_CALL destroy_render_pass(
    VkDevice device, VkRenderPass render_pass,
    const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)render_pass;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_framebuffer(
    VkDevice device, VkFramebuffer framebuffer,
    const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)framebuffer;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_command_pool(
    VkDevice device, VkCommandPool command_pool,
    const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)command_pool;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_fence(VkDevice device, VkFence fence,
                                     const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)fence;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_semaphore(
    VkDevice device, VkSemaphore semaphore,
    const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)semaphore;
    (void)allocator;
    ++destroys;
}

static const struct frame_pacer_hud_draw_provider provider = {
    .create_render_pass = create_render_pass,
    .destroy_render_pass = destroy_render_pass,
    .create_framebuffer = create_framebuffer,
    .destroy_framebuffer = destroy_framebuffer,
    .create_command_pool = create_command_pool,
    .destroy_command_pool = destroy_command_pool,
    .allocate_command_buffers = allocate_command_buffers,
    .create_fence = create_fence,
    .destroy_fence = destroy_fence,
    .create_semaphore = create_semaphore,
    .destroy_semaphore = destroy_semaphore,
};

int main(void)
{
    const struct frame_pacer_hud_image_views image_views = {
        .count = 2,
        .ready = true,
        .views = {
            (VkImageView)(uintptr_t)1,
            (VkImageView)(uintptr_t)2,
        },
    };
    const VkDevice device = (VkDevice)(uintptr_t)1;
    const VkExtent2D extent = {1280, 720};
    struct frame_pacer_hud_draw_resources resources;

    creates = destroys = fail_at = 0;
    assert(frame_pacer_hud_create_draw_resources(
        &resources, &provider, device, &image_views,
        VK_FORMAT_B8G8R8A8_SRGB, extent, 0));
    assert(resources.ready && resources.count == 2);
    frame_pacer_hud_destroy_draw_resources(&resources, &provider, device, 0);
    assert(!resources.ready && destroys == 8);

    creates = destroys = 0;
    fail_at = 3;
    assert(!frame_pacer_hud_create_draw_resources(
        &resources, &provider, device, &image_views,
        VK_FORMAT_B8G8R8A8_SRGB, extent, 0));
    assert(!resources.ready && destroys == 3);
    return 0;
}
