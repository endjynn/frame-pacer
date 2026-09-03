#include "hud_vulkan_resources.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct fake_provider {
    VkImage images[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES + 1];
    uint32_t image_count;
    uint32_t create_calls;
    uint32_t destroy_calls;
    uint32_t fail_create_at;
    VkImageView destroyed[FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES];
};

static struct fake_provider fake;

static VkResult VKAPI_CALL fake_get_images(VkDevice device,
                                           VkSwapchainKHR swapchain,
                                           uint32_t *count, VkImage *images)
{
    uint32_t index;
    (void)device;
    (void)swapchain;
    if (!images) {
        *count = fake.image_count;
        return VK_SUCCESS;
    }
    if (*count < fake.image_count)
        return VK_INCOMPLETE;
    for (index = 0; index < fake.image_count; ++index)
        images[index] = fake.images[index];
    *count = fake.image_count;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL
fake_create_view(VkDevice device, const VkImageViewCreateInfo *info,
                 const VkAllocationCallbacks *allocator, VkImageView *view)
{
    uintptr_t value;
    (void)device;
    (void)allocator;
    assert(info->viewType == VK_IMAGE_VIEW_TYPE_2D);
    assert(info->format == VK_FORMAT_B8G8R8A8_SRGB);
    assert(info->subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    ++fake.create_calls;
    if (fake.fail_create_at == fake.create_calls)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    value = (uintptr_t)0x100 + fake.create_calls;
    *view = (VkImageView)value;
    return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_view(VkDevice device, VkImageView view,
                                         const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)allocator;
    assert(fake.destroy_calls < FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES);
    fake.destroyed[fake.destroy_calls++] = view;
}

static const struct frame_pacer_hud_vulkan_provider provider = {
    .get_swapchain_images = fake_get_images,
    .create_image_view = fake_create_view,
    .destroy_image_view = fake_destroy_view,
};

static void reset(uint32_t image_count)
{
    uint32_t index;
    memset(&fake, 0, sizeof(fake));
    fake.image_count = image_count;
    for (index = 0; index < image_count; ++index)
        fake.images[index] = (VkImage)(uintptr_t)(index + 1);
}

static enum frame_pacer_hud_resource_status
setup(struct frame_pacer_hud_image_views *resources,
      const struct frame_pacer_hud_vulkan_provider *functions,
      VkImageUsageFlags usage)
{
    return frame_pacer_hud_create_image_views(
        resources, functions, (VkDevice)(uintptr_t)1,
        (VkSwapchainKHR)(uintptr_t)2, VK_FORMAT_B8G8R8A8_SRGB, usage);
}

int main(void)
{
    struct frame_pacer_hud_image_views resources;
    struct frame_pacer_hud_vulkan_provider missing = provider;

    reset(2);
    assert(setup(&resources, &provider, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_READY);
    assert(resources.ready && resources.count == 2 && fake.create_calls == 2);
    frame_pacer_hud_destroy_image_views(&resources, &provider,
                                        (VkDevice)(uintptr_t)1, 0);
    assert(!resources.ready && resources.count == 0 && fake.destroy_calls == 2);

    reset(2);
    missing.create_image_view = 0;
    assert(setup(&resources, &missing, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_MISSING_FUNCTION);
    assert(fake.create_calls == 0 && fake.destroy_calls == 0);

    reset(2);
    assert(setup(&resources, &provider, 0) ==
           FRAME_PACER_HUD_RESOURCE_NO_COLOR_ATTACHMENT);
    assert(fake.create_calls == 0);

    reset(FRAME_PACER_HUD_MAX_SWAPCHAIN_IMAGES + 1);
    assert(setup(&resources, &provider, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_IMAGE_COUNT_INVALID);
    assert(fake.create_calls == 0 && fake.destroy_calls == 0);

    reset(3);
    fake.fail_create_at = 2;
    assert(setup(&resources, &provider, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_VIEW_CREATE_FAILED);
    assert(!resources.ready && resources.count == 0 && fake.create_calls == 2);
    assert(fake.destroy_calls == 1);

    reset(1);
    assert(setup(&resources, &provider, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_READY);
    frame_pacer_hud_destroy_image_views(&resources, &provider,
                                        (VkDevice)(uintptr_t)1, 0);
    reset(2);
    assert(setup(&resources, &provider, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
           FRAME_PACER_HUD_RESOURCE_READY);
    assert(resources.count == 2 && fake.create_calls == 2);
    frame_pacer_hud_destroy_image_views(&resources, &provider,
                                        (VkDevice)(uintptr_t)1, 0);
    assert(fake.destroy_calls == 2);
    return 0;
}
