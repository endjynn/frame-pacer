#include "hud_vulkan_device.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const char *missing_command;
static unsigned int device_lookups;
static unsigned int memory_queries;

static VKAPI_ATTR void VKAPI_CALL dummy_command(void) {}

static VKAPI_ATTR void VKAPI_CALL fake_get_memory_properties(
    VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties *properties)
{
    assert(physical == (VkPhysicalDevice)(uintptr_t)2);
    ++memory_queries;
    memset(properties, 0, sizeof(*properties));
    properties->memoryTypeCount = 1;
}

static PFN_vkVoidFunction VKAPI_CALL fake_gdpa(VkDevice device,
                                               const char *name)
{
    assert(device == (VkDevice)(uintptr_t)1);
    assert(name);
    ++device_lookups;
    if (missing_command && !strcmp(name, missing_command))
        return 0;
    return dummy_command;
}

static PFN_vkVoidFunction VKAPI_CALL fake_gipa(VkInstance instance,
                                               const char *name)
{
    assert(instance == (VkInstance)(uintptr_t)3);
    if (!strcmp(name, "vkGetPhysicalDeviceMemoryProperties"))
        return (PFN_vkVoidFunction)fake_get_memory_properties;
    if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return dummy_command;
    return 0;
}

int main(void)
{
    struct frame_pacer_hud_vulkan_device hud;
    struct frame_pacer_metrics_snapshot snapshot;

    frame_pacer_hud_vulkan_device_init(
        &hud, (VkDevice)(uintptr_t)1, (VkPhysicalDevice)(uintptr_t)2, fake_gdpa,
        (VkInstance)(uintptr_t)3, fake_gipa, 0);
    assert(hud.commands_ready);
    assert(device_lookups == FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT);
    assert(memory_queries == 1);
    assert(hud.has_memory_properties);
    assert(hud.memory_properties.memoryTypeCount == 1);
    assert(hud.get_surface_capabilities);
    assert(hud.resources.get_swapchain_images ==
           (PFN_vkGetSwapchainImagesKHR)hud.commands
               .functions[FRAME_PACER_HUD_COMMAND_GET_SWAPCHAIN_IMAGES]);
    assert(hud.draw.create_render_pass ==
           (PFN_vkCreateRenderPass)hud.commands
               .functions[FRAME_PACER_HUD_COMMAND_CREATE_RENDER_PASS]);
    assert(hud.pipeline.create_shader_module ==
           (PFN_vkCreateShaderModule)hud.commands
               .functions[FRAME_PACER_HUD_COMMAND_CREATE_SHADER_MODULE]);
    assert(hud.vertex_buffer.create_buffer ==
           (PFN_vkCreateBuffer)
               hud.commands.functions[FRAME_PACER_HUD_COMMAND_CREATE_BUFFER]);
    assert(hud.record.draw ==
           (PFN_vkCmdDraw)hud.commands.functions[FRAME_PACER_HUD_COMMAND_DRAW]);
    assert(hud.present.queue_submit ==
           (PFN_vkQueueSubmit)
               hud.commands.functions[FRAME_PACER_HUD_COMMAND_QUEUE_SUBMIT]);

    frame_pacer_hud_vulkan_device_metrics_snapshot(&hud, 100, &snapshot);
    assert(hud.metrics.request_ns == 100);
    /* A discontinuity requests a reset instead of underflowing the cadence. */
    frame_pacer_hud_vulkan_device_metrics_snapshot(&hud, 99, &snapshot);
    assert(hud.metrics.request_ns == 99);
    frame_pacer_hud_vulkan_device_metrics_snapshot(&hud, 99, 0);
    frame_pacer_hud_vulkan_device_destroy(&hud);
    frame_pacer_hud_vulkan_device_destroy(&hud);

    missing_command = "vkCmdDraw";
    device_lookups = 0;
    memory_queries = 0;
    frame_pacer_hud_vulkan_device_init(
        &hud, (VkDevice)(uintptr_t)1, (VkPhysicalDevice)(uintptr_t)2, fake_gdpa,
        (VkInstance)(uintptr_t)3, fake_gipa, 0);
    assert(!hud.commands_ready);
    assert(!hud.record.draw);
    assert(device_lookups == FRAME_PACER_HUD_COMMAND_DRAW + 1U);
    frame_pacer_hud_vulkan_device_destroy(&hud);

    frame_pacer_hud_vulkan_device_init(0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0,
                                       VK_NULL_HANDLE, 0, 0);
    frame_pacer_hud_vulkan_device_destroy(0);
    frame_pacer_hud_vulkan_device_metrics_snapshot(0, 0, &snapshot);
    return 0;
}
