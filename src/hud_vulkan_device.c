#include "hud_vulkan_device.h"

#include <string.h>

#define COMMAND(device_, command_, type_) \
    ((type_)(device_)->commands.functions[command_])

void frame_pacer_hud_vulkan_device_init(
    struct frame_pacer_hud_vulkan_device *hud, VkDevice device,
    VkPhysicalDevice physical, PFN_vkGetDeviceProcAddr gdpa,
    VkInstance instance, PFN_vkGetInstanceProcAddr gipa,
    unsigned int process_id)
{
    PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties;

    if (!hud)
        return;
    memset(hud, 0, sizeof(*hud));
    frame_pacer_hud_metrics_cache_init(&hud->metrics, 0, process_id);
    if (!device || !physical || !gdpa)
        return;

    hud->commands_ready = frame_pacer_hud_resolve_commands(
        &hud->commands, gdpa, device);
    hud->resources = (struct frame_pacer_hud_vulkan_provider){
        .get_swapchain_images = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_GET_SWAPCHAIN_IMAGES,
            PFN_vkGetSwapchainImagesKHR),
        .create_image_view = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_IMAGE_VIEW,
            PFN_vkCreateImageView),
        .destroy_image_view = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_IMAGE_VIEW,
            PFN_vkDestroyImageView),
    };
    hud->draw = (struct frame_pacer_hud_draw_provider){
        .create_render_pass = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_RENDER_PASS,
            PFN_vkCreateRenderPass),
        .destroy_render_pass = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_RENDER_PASS,
            PFN_vkDestroyRenderPass),
        .create_framebuffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_FRAMEBUFFER,
            PFN_vkCreateFramebuffer),
        .destroy_framebuffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_FRAMEBUFFER,
            PFN_vkDestroyFramebuffer),
        .create_command_pool = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_COMMAND_POOL,
            PFN_vkCreateCommandPool),
        .destroy_command_pool = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_COMMAND_POOL,
            PFN_vkDestroyCommandPool),
        .allocate_command_buffers = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_ALLOCATE_COMMAND_BUFFERS,
            PFN_vkAllocateCommandBuffers),
        .create_fence = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_FENCE, PFN_vkCreateFence),
        .destroy_fence = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_FENCE, PFN_vkDestroyFence),
        .create_semaphore = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_SEMAPHORE,
            PFN_vkCreateSemaphore),
        .destroy_semaphore = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_SEMAPHORE,
            PFN_vkDestroySemaphore),
    };
    hud->pipeline = (struct frame_pacer_hud_pipeline_provider){
        .create_shader_module = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_SHADER_MODULE,
            PFN_vkCreateShaderModule),
        .destroy_shader_module = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_SHADER_MODULE,
            PFN_vkDestroyShaderModule),
        .create_pipeline_layout = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_PIPELINE_LAYOUT,
            PFN_vkCreatePipelineLayout),
        .destroy_pipeline_layout = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE_LAYOUT,
            PFN_vkDestroyPipelineLayout),
        .create_graphics_pipelines = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_GRAPHICS_PIPELINES,
            PFN_vkCreateGraphicsPipelines),
        .destroy_pipeline = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE,
            PFN_vkDestroyPipeline),
    };
    hud->vertex_buffer = (struct frame_pacer_hud_vertex_buffer_provider){
        .create_buffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_CREATE_BUFFER, PFN_vkCreateBuffer),
        .destroy_buffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_DESTROY_BUFFER, PFN_vkDestroyBuffer),
        .allocate_memory = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_ALLOCATE_MEMORY, PFN_vkAllocateMemory),
        .free_memory = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_FREE_MEMORY, PFN_vkFreeMemory),
        .map_memory = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_MAP_MEMORY, PFN_vkMapMemory),
        .unmap_memory = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_UNMAP_MEMORY, PFN_vkUnmapMemory),
        .bind_memory = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_BIND_BUFFER_MEMORY,
            PFN_vkBindBufferMemory),
        .get_requirements = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_GET_BUFFER_MEMORY_REQUIREMENTS,
            PFN_vkGetBufferMemoryRequirements),
    };
    hud->record = (struct frame_pacer_hud_record_provider){
        .reset_command_buffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_RESET_COMMAND_BUFFER,
            PFN_vkResetCommandBuffer),
        .begin_command_buffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_BEGIN_COMMAND_BUFFER,
            PFN_vkBeginCommandBuffer),
        .end_command_buffer = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_END_COMMAND_BUFFER,
            PFN_vkEndCommandBuffer),
        .pipeline_barrier = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_PIPELINE_BARRIER,
            PFN_vkCmdPipelineBarrier),
        .begin_render_pass = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_BEGIN_RENDER_PASS,
            PFN_vkCmdBeginRenderPass),
        .end_render_pass = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_END_RENDER_PASS,
            PFN_vkCmdEndRenderPass),
        .bind_pipeline = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_BIND_PIPELINE,
            PFN_vkCmdBindPipeline),
        .bind_vertex_buffers = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_BIND_VERTEX_BUFFERS,
            PFN_vkCmdBindVertexBuffers),
        .draw = COMMAND(hud, FRAME_PACER_HUD_COMMAND_DRAW, PFN_vkCmdDraw),
        .set_viewport = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_SET_VIEWPORT, PFN_vkCmdSetViewport),
        .set_scissor = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_SET_SCISSOR, PFN_vkCmdSetScissor),
        .push_constants = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_PUSH_CONSTANTS,
            PFN_vkCmdPushConstants),
    };
    hud->present = (struct frame_pacer_hud_present_provider){
        .wait_for_fences = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_WAIT_FOR_FENCES, PFN_vkWaitForFences),
        .reset_fences = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_RESET_FENCES, PFN_vkResetFences),
        .queue_submit = COMMAND(
            hud, FRAME_PACER_HUD_COMMAND_QUEUE_SUBMIT, PFN_vkQueueSubmit),
    };

    if (!instance || !gipa)
        return;
    hud->get_surface_capabilities =
        (frame_pacer_hud_surface_capabilities_fn)gipa(
            instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    get_memory_properties = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(
        instance, "vkGetPhysicalDeviceMemoryProperties");
    if (get_memory_properties) {
        get_memory_properties(physical, &hud->memory_properties);
        hud->has_memory_properties = true;
    }
}

void frame_pacer_hud_vulkan_device_destroy(
    struct frame_pacer_hud_vulkan_device *hud)
{
    if (!hud)
        return;
    frame_pacer_hud_metrics_cache_destroy(&hud->metrics);
}

void frame_pacer_hud_vulkan_device_metrics_snapshot(
    struct frame_pacer_hud_vulkan_device *hud, uint64_t now_ns,
    struct frame_pacer_metrics_snapshot *snapshot)
{
    frame_pacer_hud_metrics_cache_snapshot(
        hud ? &hud->metrics : 0, now_ns, snapshot);
}

#undef COMMAND
