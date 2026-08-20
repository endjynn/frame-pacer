#ifndef FRAME_PACER_HUD_SWAPCHAIN_POLICY_H
#define FRAME_PACER_HUD_SWAPCHAIN_POLICY_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef VkResult (*frame_pacer_hud_surface_capabilities_fn)(
    VkPhysicalDevice physical_device, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *capabilities);
typedef VkResult (*frame_pacer_hud_create_swapchain_fn)(
    VkDevice device, const VkSwapchainCreateInfoKHR *create_info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain);

struct frame_pacer_hud_swapchain_result {
    VkResult result;
    bool color_attachment_enabled;
    bool retried_original;
};

/* The caller's create-info is never written. If the surface explicitly
 * supports it, a stack copy gains colour-attachment usage for the HUD. */
struct frame_pacer_hud_swapchain_result frame_pacer_hud_create_swapchain(
    frame_pacer_hud_surface_capabilities_fn get_surface_capabilities,
    frame_pacer_hud_create_swapchain_fn create_swapchain,
    VkPhysicalDevice physical_device, VkDevice device,
    const VkSwapchainCreateInfoKHR *create_info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain);

#endif
