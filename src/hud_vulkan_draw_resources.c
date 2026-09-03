#include "hud_vulkan_draw_resources.h"

#include <string.h>

static bool valid_provider(const struct frame_pacer_hud_draw_provider *provider)
{
    return provider && provider->create_render_pass &&
           provider->destroy_render_pass && provider->create_framebuffer &&
           provider->destroy_framebuffer && provider->create_command_pool &&
           provider->destroy_command_pool &&
           provider->allocate_command_buffers && provider->create_fence &&
           provider->destroy_fence && provider->create_semaphore &&
           provider->destroy_semaphore;
}

void frame_pacer_hud_destroy_draw_resources(
    struct frame_pacer_hud_draw_resources *resources,
    const struct frame_pacer_hud_draw_provider *provider, VkDevice device,
    const VkAllocationCallbacks *allocator)
{
    uint32_t index;

    if (!resources)
        return;

    if (valid_provider(provider)) {
        for (index = 0; index < resources->count; ++index) {
            if (resources->semaphores[index])
                provider->destroy_semaphore(
                    device, resources->semaphores[index], allocator);
            if (resources->fences[index])
                provider->destroy_fence(device, resources->fences[index],
                                        allocator);
            if (resources->framebuffers[index])
                provider->destroy_framebuffer(
                    device, resources->framebuffers[index], allocator);
        }
        if (resources->command_pool)
            provider->destroy_command_pool(device, resources->command_pool,
                                           allocator);
        if (resources->render_pass)
            provider->destroy_render_pass(device, resources->render_pass,
                                          allocator);
    }
    memset(resources, 0, sizeof(*resources));
}

bool frame_pacer_hud_create_draw_resources(
    struct frame_pacer_hud_draw_resources *resources,
    const struct frame_pacer_hud_draw_provider *provider, VkDevice device,
    const struct frame_pacer_hud_image_views *image_views, VkFormat format,
    VkExtent2D extent, uint32_t queue_family)
{
    const VkAttachmentDescription attachment = {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference color_attachment = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };
    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    };
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    uint32_t index;

    if (!resources || !image_views || !image_views->ready ||
        !image_views->count ||
        image_views->count > FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES ||
        !valid_provider(provider))
        return false;

    memset(resources, 0, sizeof(*resources));
    resources->count = image_views->count;
    if (provider->create_render_pass(device, &render_pass_info, 0,
                                     &resources->render_pass) != VK_SUCCESS)
        goto fail;

    for (index = 0; index < resources->count; ++index) {
        const VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = resources->render_pass,
            .attachmentCount = 1,
            .pAttachments = &image_views->views[index],
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };

        if (provider->create_framebuffer(device, &framebuffer_info, 0,
                                         &resources->framebuffers[index]) !=
            VK_SUCCESS)
            goto fail;
    }
    if (provider->create_command_pool(device, &command_pool_info, 0,
                                      &resources->command_pool) != VK_SUCCESS)
        goto fail;

    command_buffer_info.commandPool = resources->command_pool;
    command_buffer_info.commandBufferCount = resources->count;
    if (provider->allocate_command_buffers(device, &command_buffer_info,
                                           resources->command_buffers) !=
        VK_SUCCESS)
        goto fail;

    for (index = 0; index < resources->count; ++index) {
        if (provider->create_fence(device, &fence_info, 0,
                                   &resources->fences[index]) != VK_SUCCESS ||
            provider->create_semaphore(device, &semaphore_info, 0,
                                       &resources->semaphores[index]) !=
                VK_SUCCESS)
            goto fail;
    }

    resources->ready = true;
    return true;

fail:
    frame_pacer_hud_destroy_draw_resources(resources, provider, device, 0);
    return false;
}
