#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice *physical_devices = 0;
    VkQueueFamilyProperties *queue_families = 0;
    uint32_t physical_count = 0;
    uint32_t queue_family_count = 0;
    uint32_t queue_family = UINT32_MAX;
    VkResult result;
    int exit_code = 1;

    result = vkCreateInstance(&instance_info, 0, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "instance %d\n", result);
        goto cleanup;
    }
    result = vkEnumeratePhysicalDevices(instance, &physical_count, 0);
    if (result != VK_SUCCESS || !physical_count) {
        fprintf(stderr, "physical %d count %u\n", result, physical_count);
        goto cleanup;
    }
    physical_devices = calloc(physical_count, sizeof(*physical_devices));
    if (!physical_devices) goto cleanup;
    result = vkEnumeratePhysicalDevices(instance, &physical_count,
                                         physical_devices);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "enumerate %d\n", result);
        goto cleanup;
    }
    physical = physical_devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count, 0);
    queue_families = calloc(queue_family_count, sizeof(*queue_families));
    if (!queue_families) goto cleanup;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count,
                                             queue_families);
    for (queue_family = 0; queue_family < queue_family_count; ++queue_family)
        if (queue_families[queue_family].queueCount) break;
    if (queue_family == queue_family_count) {
        fputs("no queue family\n", stderr);
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
        };

        result = vkCreateDevice(physical, &device_info, 0, &device);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "device %d\n", result);
            goto cleanup;
        }
    }
    {
        VkQueue queue;

        vkGetDeviceQueue(device, queue_family, 0, &queue);
        if (!queue) {
            fputs("queue unavailable\n", stderr);
            goto cleanup;
        }
    }
    exit_code = 0;

cleanup:
    if (device) vkDestroyDevice(device, 0);
    free(queue_families);
    free(physical_devices);
    if (instance) vkDestroyInstance(instance, 0);
    return exit_code;
}
