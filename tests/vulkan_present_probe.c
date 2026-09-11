#define _POSIX_C_SOURCE 200809L
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <X11/Xlib.h>

#include "present_extent.h"
#include "present_benchmark.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *operation, VkResult result)
{
    fprintf(stderr, "%s: %d\n", operation, result);
    return 1;
}

static VkResult clear_image(VkCommandBuffer command, VkImage image,
                            VkQueue queue, VkSemaphore acquired,
                            VkSemaphore ready)
{
    VkResult result = vkResetCommandBuffer(command, 0);
    if (result != VK_SUCCESS)
        return result;
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    result = vkBeginCommandBuffer(command, &begin);
    if (result != VK_SUCCESS)
        return result;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
    const VkClearColorValue color = {.float32 = {0.1f, 0.2f, 0.3f, 1.0f}};
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS)
        return result;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .waitSemaphoreCount = 1,
                                 .pWaitSemaphores = &acquired,
                                 .pWaitDstStageMask = &stage,
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &command,
                                 .signalSemaphoreCount = 1,
                                 .pSignalSemaphores = &ready};
    return vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
}

int main(void)
{
    struct present_benchmark benchmark;
    benchmark_init(&benchmark, PRESENT_CONTENT_WIDTH + 32U,
                   PRESENT_CONTENT_HEIGHT + 32U);
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
    VkSemaphore ready = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkImage *images = NULL;
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
    int resizing = 0;

    display = XOpenDisplay(0);
    if (!display) {
        fputs("X11 display unavailable\n", stderr);
        return 77;
    }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                 benchmark.width, benchmark.height, 0, 0, 0);
    if (!window)
        goto cleanup;
    XSetWindowAttributes window_settings = {.override_redirect = True};
    XChangeWindowAttributes(display, window, CWOverrideRedirect,
                            &window_settings);
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

        if (!devices)
            goto cleanup;
        result = vkEnumeratePhysicalDevices(instance, &physical_count, devices);
        if (result != VK_SUCCESS) {
            free(devices);
            exit_code = fail("vkEnumeratePhysicalDevices", result);
            goto cleanup;
        }
        for (physical_index = 0; physical_index < physical_count;
             ++physical_index) {
            uint32_t family_count = 0;
            VkQueueFamilyProperties *families;
            uint32_t family;

            vkGetPhysicalDeviceQueueFamilyProperties(devices[physical_index],
                                                     &family_count, 0);
            families = calloc(family_count, sizeof(*families));
            if (!families)
                continue;
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
            if (physical)
                break;
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
resize_setup:
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
    if (!formats)
        goto cleanup;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
                                                  &format_count, formats);
    if (result != VK_SUCCESS) {
        exit_code = fail("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
        goto cleanup;
    }
    extent = capabilities.currentExtent;
    if (getenv("FRAME_PACER_BENCH_FRAMES")) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        fprintf(stderr,
                "vk_device=%s\nvk_driver_version=%u\nvk_device_type=%u\n",
                properties.deviceName, properties.driverVersion,
                (unsigned int)properties.deviceType);
    }
    if (extent.width == UINT32_MAX) {
        extent.width = benchmark.width;
        extent.height = benchmark.height;
    }
    if (extent.width != benchmark.width || extent.height != benchmark.height ||
        !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        fputs("Vulkan probe requires exact extent and transfer-destination "
              "images\n",
              stderr);
        goto cleanup;
    }
    if (extent.width < PRESENT_CONTENT_WIDTH ||
        extent.height < PRESENT_CONTENT_HEIGHT) {
        fputs("Vulkan swapchain cannot contain the complete HUD\n", stderr);
        goto cleanup;
    }
    if (!(capabilities.supportedCompositeAlpha & composite_alpha)) {
        VkCompositeAlphaFlagsKHR supported =
            capabilities.supportedCompositeAlpha;
        composite_alpha =
            (VkCompositeAlphaFlagBitsKHR)(supported & (~supported + 1U));
    }
    {
        uint32_t image_count = capabilities.minImageCount + 1;
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (getenv("FRAME_PACER_BENCH_FRAMES")) {
            uint32_t mode_count = 0;
            VkPresentModeKHR *modes;
            if (vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physical, surface, &mode_count, 0) != VK_SUCCESS)
                goto cleanup;
            modes = calloc(mode_count, sizeof(*modes));
            if (!modes)
                goto cleanup;
            result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                physical, surface, &mode_count, modes);
            if (result == VK_SUCCESS)
                for (uint32_t index = 0; index < mode_count; ++index)
                    if (modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                        mode = modes[index];
            free(modes);
            if (result != VK_SUCCESS || mode != VK_PRESENT_MODE_IMMEDIATE_KHR) {
                fputs("Benchmark requires immediate presentation\n", stderr);
                goto cleanup;
            }
        }
        VkSwapchainCreateInfoKHR swapchain_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .imageFormat = formats[0].format,
            .imageColorSpace = formats[0].colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = composite_alpha,
            .presentMode = mode,
            .clipped = VK_TRUE,
        };

        if (capabilities.maxImageCount &&
            image_count > capabilities.maxImageCount)
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
        result = vkCreateSemaphore(device, &semaphore_info, 0, &ready);
        if (result != VK_SUCCESS)
            goto cleanup;
        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue_family};
        if (vkCreateCommandPool(device, &pool_info, NULL, &command_pool) !=
            VK_SUCCESS)
            goto cleanup;
        const VkCommandBufferAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1};
        if (vkAllocateCommandBuffers(device, &allocation, &command) !=
            VK_SUCCESS)
            goto cleanup;
        uint32_t count = 0;
        if (vkGetSwapchainImagesKHR(device, swapchain, &count, NULL) !=
                VK_SUCCESS ||
            !count)
            goto cleanup;
        images = calloc(count, sizeof(*images));
        if (!images || vkGetSwapchainImagesKHR(device, swapchain, &count,
                                               images) != VK_SUCCESS)
            goto cleanup;
    }
    for (frame = benchmark.count; frame < benchmark.frames; ++frame) {
        if (!resizing)
            benchmark_begin(&benchmark);
        resizing = 0;
        uint32_t image_index;
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &ready,
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
        result =
            clear_image(command, images[image_index], queue, acquired, ready);
        if (result != VK_SUCCESS) {
            exit_code = fail("clear_image", result);
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
        benchmark_end(&benchmark);
        if (benchmark_resize_due(&benchmark)) {
            benchmark_begin(&benchmark);
            benchmark_resize(&benchmark);
            XResizeWindow(display, window, benchmark.width, benchmark.height);
            XSync(display, False);
            vkDestroySemaphore(device, acquired, NULL);
            vkDestroySemaphore(device, ready, NULL);
            vkDestroyCommandPool(device, command_pool, NULL);
            vkDestroySwapchainKHR(device, swapchain, NULL);
            free(images);
            free(formats);
            acquired = ready = VK_NULL_HANDLE;
            command_pool = VK_NULL_HANDLE;
            swapchain = VK_NULL_HANDLE;
            images = NULL;
            formats = NULL;
            resizing = 1;
            goto resize_setup;
        }
    }
    benchmark_report(&benchmark);
    exit_code = 0;

cleanup:
    if (device)
        (void)vkDeviceWaitIdle(device);
    if (acquired)
        vkDestroySemaphore(device, acquired, 0);
    if (ready)
        vkDestroySemaphore(device, ready, 0);
    if (command_pool)
        vkDestroyCommandPool(device, command_pool, 0);
    free(images);
    if (swapchain)
        vkDestroySwapchainKHR(device, swapchain, 0);
    if (device)
        vkDestroyDevice(device, 0);
    free(formats);
    if (surface)
        vkDestroySurfaceKHR(instance, surface, 0);
    if (instance)
        vkDestroyInstance(instance, 0);
    if (window)
        XDestroyWindow(display, window);
    if (display)
        XCloseDisplay(display);
    return exit_code;
}
