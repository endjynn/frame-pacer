#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <X11/Xlib.h>

#include "hud_vertices.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *operation, VkResult result)
{
    fprintf(stderr, "%s: %d\n", operation, result);
    return 1;
}

int main(void)
{
    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "frame-pacer-present-probe",
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instance_extensions,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    Display *display = 0;
    Window window = 0;
    VkResult result;
    int exit_code = 1;
    uint32_t physical_count = 0;
    uint32_t queue_family = UINT32_MAX;
    uint32_t format_count = 0;
    VkSurfaceFormatKHR *formats = 0;
    VkSurfaceCapabilitiesKHR capabilities;
    VkExtent2D extent;
    VkCompositeAlphaFlagBitsKHR composite_alpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    uint32_t frame;

    display = XOpenDisplay(0);
    if (!display) {
        fputs("X11 display unavailable\n", stderr);
        return 77;
    }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                 FRAME_PACER_HUD_WIDTH_MAX + 32U,
                                 FRAME_PACER_HUD_HEIGHT_MAX + 32U, 0, 0, 0);
    if (!window) goto cleanup;
    XMapWindow(display, window);
    XSync(display, False);

    result = vkCreateInstance(&instance_info, 0, &instance);
    if (result != VK_SUCCESS) {
        exit_code = fail("vkCreateInstance", result);
        goto cleanup;
    }
    {
        VkXlibSurfaceCreateInfoKHR surface_info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = display,
            .window = window,
        };

        result = vkCreateXlibSurfaceKHR(instance, &surface_info, 0, &surface);
        if (result != VK_SUCCESS) {
            exit_code = fail("vkCreateXlibSurfaceKHR", result);
            goto cleanup;
        }
    }
    result = vkEnumeratePhysicalDevices(instance, &physical_count, 0);
    if (result != VK_SUCCESS || !physical_count) {
        exit_code = fail("vkEnumeratePhysicalDevices", result);
        goto cleanup;
    }
    {
        VkPhysicalDevice *devices = calloc(physical_count, sizeof(*devices));
        uint32_t physical_index;

        if (!devices) goto cleanup;
        result = vkEnumeratePhysicalDevices(instance, &physical_count, devices);
        if (result != VK_SUCCESS) {
            free(devices);
            exit_code = fail("vkEnumeratePhysicalDevices", result);
            goto cleanup;
        }
        for (physical_index = 0; physical_index < physical_count; ++physical_index) {
            uint32_t family_count = 0;
            VkQueueFamilyProperties *families;
            uint32_t family;

            vkGetPhysicalDeviceQueueFamilyProperties(devices[physical_index],
                                                     &family_count, 0);
            families = calloc(family_count, sizeof(*families));
            if (!families) continue;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[physical_index],
                                                     &family_count, families);
            for (family = 0; family < family_count; ++family) {
                VkBool32 present = VK_FALSE;

                result = vkGetPhysicalDeviceSurfaceSupportKHR(
                    devices[physical_index], family, surface, &present);
                if (result == VK_SUCCESS && present &&
                    (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical = devices[physical_index];
                    queue_family = family;
                    break;
                }
            }
            free(families);
            if (physical) break;
        }
        free(devices);
    }
    if (!physical) {
        fputs("no graphics/present queue family\n", stderr);
        goto cleanup;
    }
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queue_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        VkDeviceCreateInfo device_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledExtensionCount = 1,
            .ppEnabledExtensionNames = device_extensions,
        };

        result = vkCreateDevice(physical, &device_info, 0, &device);
        if (result != VK_SUCCESS) {
            exit_code = fail("vkCreateDevice", result);
            goto cleanup;
        }
    }
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface,
                                                        &capabilities);
    if (result != VK_SUCCESS) {
        exit_code = fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        goto cleanup;
    }
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
                                                   &format_count, 0);
    if (result != VK_SUCCESS || !format_count) {
        exit_code = fail("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
        goto cleanup;
    }
    formats = calloc(format_count, sizeof(*formats));
    if (!formats) goto cleanup;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
                                                   &format_count, formats);
    if (result != VK_SUCCESS) {
        exit_code = fail("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
        goto cleanup;
    }
    extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = FRAME_PACER_HUD_WIDTH_MAX + 32U;
        extent.height = FRAME_PACER_HUD_HEIGHT_MAX + 32U;
    }
    if (extent.width < FRAME_PACER_HUD_WIDTH_MAX ||
        extent.height < FRAME_PACER_HUD_HEIGHT_MAX) {
        fputs("Vulkan swapchain cannot contain the complete HUD\n", stderr);
        goto cleanup;
    }
    if (!(capabilities.supportedCompositeAlpha & composite_alpha)) {
        VkCompositeAlphaFlagsKHR supported = capabilities.supportedCompositeAlpha;
        composite_alpha = (VkCompositeAlphaFlagBitsKHR)(supported & (~supported + 1U));
    }
    {
        uint32_t image_count = capabilities.minImageCount + 1;
        VkSwapchainCreateInfoKHR swapchain_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .imageFormat = formats[0].format,
            .imageColorSpace = formats[0].colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = composite_alpha,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
        };

        if (capabilities.maxImageCount && image_count > capabilities.maxImageCount)
            image_count = capabilities.maxImageCount;
        swapchain_info.minImageCount = image_count;
        result = vkCreateSwapchainKHR(device, &swapchain_info, 0, &swapchain);
        if (result != VK_SUCCESS) {
            exit_code = fail("vkCreateSwapchainKHR", result);
            goto cleanup;
        }
    }
    {
        VkSemaphoreCreateInfo semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        result = vkCreateSemaphore(device, &semaphore_info, 0, &acquired);
        if (result != VK_SUCCESS) {
            exit_code = fail("vkCreateSemaphore", result);
            goto cleanup;
        }
    }
    for (frame = 0; frame < 2; ++frame) {
        uint32_t image_index;
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquired,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
        };

        result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
                                       VK_NULL_HANDLE, &image_index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            exit_code = fail("vkAcquireNextImageKHR", result);
            goto cleanup;
        }
        result = vkQueuePresentKHR(queue, &present_info);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            exit_code = fail("vkQueuePresentKHR", result);
            goto cleanup;
        }
        result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            exit_code = fail("vkQueueWaitIdle", result);
            goto cleanup;
        }
    }
    exit_code = 0;

cleanup:
    if (device) (void)vkDeviceWaitIdle(device);
    if (acquired) vkDestroySemaphore(device, acquired, 0);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, 0);
    if (device) vkDestroyDevice(device, 0);
    free(formats);
    if (surface) vkDestroySurfaceKHR(instance, surface, 0);
    if (instance) vkDestroyInstance(instance, 0);
    if (window) XDestroyWindow(display, window);
    if (display) XCloseDisplay(display);
    return exit_code;
}
