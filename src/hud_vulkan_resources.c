#include "hud_vulkan_resources.h"

#include <string.h>

const char *frame_pacer_hud_resource_status_string(
    enum frame_pacer_hud_resource_status status)
{
    switch (status) {
    case FRAME_PACER_HUD_RESOURCE_READY:
        return "ready";
    case FRAME_PACER_HUD_RESOURCE_MISSING_FUNCTION:
        return "required Vulkan function missing";
    case FRAME_PACER_HUD_RESOURCE_NO_COLOR_ATTACHMENT:
        return "swapchain lacks color-attachment usage";
    case FRAME_PACER_HUD_RESOURCE_IMAGE_QUERY_FAILED:
        return "could not obtain swapchain images";
    case FRAME_PACER_HUD_RESOURCE_IMAGE_COUNT_INVALID:
        return "swapchain image count is unsupported";
    case FRAME_PACER_HUD_RESOURCE_VIEW_CREATE_FAILED:
        return "could not create HUD image view";
    }
    return "unknown HUD resource failure";
}

void frame_pacer_hud_destroy_image_views(
    struct frame_pacer_hud_image_views *resources,
    const struct frame_pacer_hud_vulkan_provider *provider,
    VkDevice device, const VkAllocationCallbacks *allocator)
{
    uint32_t index;

    if (!resources)
        return;
    if (provider && provider->destroy_image_view) {
        for (index = 0; index < resources->count; ++index) {
            if (resources->views[index])
                provider->destroy_image_view(device, resources->views[index], allocator);
        }
    }
    memset(resources, 0, sizeof(*resources));
}

enum frame_pacer_hud_resource_status frame_pacer_hud_create_image_views(
    struct frame_pacer_hud_image_views *resources,
    const struct frame_pacer_hud_vulkan_provider *provider,
    VkDevice device, VkSwapchainKHR swapchain, VkFormat format,
    VkImageUsageFlags image_usage)
{
    uint32_t count = 0;
    uint32_t index;
    VkResult result;

    if (!resources || !provider || !provider->get_swapchain_images ||
        !provider->create_image_view || !provider->destroy_image_view)
        return FRAME_PACER_HUD_RESOURCE_MISSING_FUNCTION;
    memset(resources, 0, sizeof(*resources));
    if (!(image_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return FRAME_PACER_HUD_RESOURCE_NO_COLOR_ATTACHMENT;

    result = provider->get_swapchain_images(device, swapchain, &count, 0);
    if (result != VK_SUCCESS)
        return FRAME_PACER_HUD_RESOURCE_IMAGE_QUERY_FAILED;
    if (!count || count > FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES)
        return FRAME_PACER_HUD_RESOURCE_IMAGE_COUNT_INVALID;
    result = provider->get_swapchain_images(device, swapchain, &count, resources->images);
    if (result != VK_SUCCESS)
        return FRAME_PACER_HUD_RESOURCE_IMAGE_QUERY_FAILED;
    if (!count || count > FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES)
        return FRAME_PACER_HUD_RESOURCE_IMAGE_COUNT_INVALID;

    for (index = 0; index < count; ++index) {
        const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = resources->images[index],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        result = provider->create_image_view(device, &view_info, 0,
                                             &resources->views[index]);
        if (result != VK_SUCCESS || !resources->views[index]) {
            resources->count = index;
            frame_pacer_hud_destroy_image_views(resources, provider, device, 0);
            return FRAME_PACER_HUD_RESOURCE_VIEW_CREATE_FAILED;
        }
    }
    resources->count = count;
    resources->ready = true;
    return FRAME_PACER_HUD_RESOURCE_READY;
}
