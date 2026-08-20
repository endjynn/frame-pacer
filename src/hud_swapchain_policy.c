#include "hud_swapchain_policy.h"

#include <string.h>

struct frame_pacer_hud_swapchain_result frame_pacer_hud_create_swapchain(
    frame_pacer_hud_surface_capabilities_fn get_surface_capabilities,
    frame_pacer_hud_create_swapchain_fn create_swapchain,
    VkPhysicalDevice physical_device, VkDevice device,
    const VkSwapchainCreateInfoKHR *create_info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
    struct frame_pacer_hud_swapchain_result outcome = {
        .result = VK_ERROR_INITIALIZATION_FAILED,
    };
    VkSurfaceCapabilitiesKHR capabilities;
    VkSwapchainCreateInfoKHR augmented;

    if (!create_swapchain || !create_info || !swapchain)
        return outcome;
    if (create_info->imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        outcome.result = create_swapchain(device, create_info, allocator, swapchain);
        outcome.color_attachment_enabled = outcome.result == VK_SUCCESS;
        return outcome;
    }
    if (!get_surface_capabilities ||
        get_surface_capabilities(physical_device, create_info->surface,
                                 &capabilities) != VK_SUCCESS ||
        !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        outcome.result = create_swapchain(device, create_info, allocator, swapchain);
        return outcome;
    }

    augmented = *create_info;
    augmented.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    outcome.result = create_swapchain(device, &augmented, allocator, swapchain);
    if (outcome.result == VK_SUCCESS) {
        outcome.color_attachment_enabled = true;
        return outcome;
    }
    /* A driver may reject a legal combination for a reason not exposed by the
     * surface capabilities. Recover the exact caller request, fail-open. */
    outcome.retried_original = true;
    outcome.result = create_swapchain(device, create_info, allocator, swapchain);
    return outcome;
}
