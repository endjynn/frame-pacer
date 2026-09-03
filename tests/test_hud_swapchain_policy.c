#include "hud_swapchain_policy.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct fake_provider {
    VkImageUsageFlags supported_usage;
    VkResult capabilities_result;
    VkResult augmented_result;
    VkResult original_result;
    uint32_t calls;
    VkImageUsageFlags usage[2];
};

static struct fake_provider fake;

static VkResult VKAPI_CALL capabilities(VkPhysicalDevice physical,
                                        VkSurfaceKHR surface,
                                        VkSurfaceCapabilitiesKHR *out)
{
    (void)physical;
    (void)surface;
    memset(out, 0, sizeof(*out));
    out->supportedUsageFlags = fake.supported_usage;
    return fake.capabilities_result;
}

static VkResult VKAPI_CALL create(VkDevice device,
                                  const VkSwapchainCreateInfoKHR *info,
                                  const VkAllocationCallbacks *allocator,
                                  VkSwapchainKHR *swapchain)
{
    VkResult result;
    (void)device;
    (void)allocator;
    assert(fake.calls < 2);
    fake.usage[fake.calls++] = info->imageUsage;
    result = (info->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                 ? fake.augmented_result
                 : fake.original_result;
    if (result == VK_SUCCESS)
        *swapchain = (VkSwapchainKHR)(uintptr_t)3;
    return result;
}

static struct frame_pacer_hud_swapchain_result
run(VkSwapchainCreateInfoKHR *info)
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    return frame_pacer_hud_create_swapchain(
        capabilities, create, (VkPhysicalDevice)(uintptr_t)1,
        (VkDevice)(uintptr_t)2, info, 0, &swapchain);
}

int main(void)
{
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = (VkSurfaceKHR)(uintptr_t)4,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    struct frame_pacer_hud_swapchain_result result;

    memset(&fake, 0, sizeof(fake));
    fake.supported_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    fake.capabilities_result = VK_SUCCESS;
    fake.augmented_result = VK_SUCCESS;
    result = run(&info);
    assert(result.result == VK_SUCCESS && result.color_attachment_enabled);
    assert(!result.retried_original && fake.calls == 1);
    assert(fake.usage[0] == (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
    assert(info.imageUsage == VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    memset(&fake, 0, sizeof(fake));
    fake.capabilities_result = VK_SUCCESS;
    fake.augmented_result = VK_SUCCESS;
    fake.original_result = VK_SUCCESS;
    result = run(&info);
    assert(result.result == VK_SUCCESS && !result.color_attachment_enabled);
    assert(fake.calls == 1 && fake.usage[0] == VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    memset(&fake, 0, sizeof(fake));
    fake.capabilities_result = VK_ERROR_SURFACE_LOST_KHR;
    fake.original_result = VK_SUCCESS;
    result = run(&info);
    assert(result.result == VK_SUCCESS && !result.color_attachment_enabled);
    assert(fake.calls == 1 && fake.usage[0] == VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    memset(&fake, 0, sizeof(fake));
    fake.supported_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    fake.capabilities_result = VK_SUCCESS;
    fake.augmented_result = VK_ERROR_FORMAT_NOT_SUPPORTED;
    fake.original_result = VK_SUCCESS;
    result = run(&info);
    assert(result.result == VK_SUCCESS && !result.color_attachment_enabled);
    assert(result.retried_original && fake.calls == 2);
    assert(fake.usage[0] & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    assert(fake.usage[1] == VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    memset(&fake, 0, sizeof(fake));
    fake.supported_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    fake.capabilities_result = VK_SUCCESS;
    fake.augmented_result = VK_SUCCESS;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    result = run(&info);
    assert(result.result == VK_SUCCESS && result.color_attachment_enabled);
    assert(fake.calls == 1 &&
           fake.usage[0] == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    return 0;
}
