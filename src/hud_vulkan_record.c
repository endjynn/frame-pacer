#include "hud_vulkan_record.h"

static bool valid(const struct frame_pacer_hud_record_provider *p)
{
    return p && p->reset_command_buffer && p->begin_command_buffer &&
           p->end_command_buffer && p->pipeline_barrier &&
           p->begin_render_pass && p->end_render_pass && p->bind_pipeline &&
           p->bind_vertex_buffers && p->draw && p->set_viewport &&
           p->set_scissor && p->push_constants;
}

bool frame_pacer_hud_record(
    const struct frame_pacer_hud_record_provider *p, VkCommandBuffer command,
    VkImage image, VkFramebuffer framebuffer, VkRenderPass pass,
    const struct frame_pacer_hud_pipeline *pipeline,
    const struct frame_pacer_hud_vertex_buffer *vertices, VkExtent2D extent,
    uint32_t vertex_count)
{
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    const VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };
    VkImageMemoryBarrier to_color = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    VkImageMemoryBarrier to_present = to_color;
    const VkRenderPassBeginInfo render = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass,
        .framebuffer = framebuffer,
        .renderArea = {.extent = extent},
    };
    const VkViewport viewport = {.width = (float)extent.width,
                                 .height = (float)extent.height,
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f};
    const VkRect2D scissor = {.extent = extent};
    const VkDeviceSize offset = 0;
    const float inverse_extent[2] = {1.0f / (float)extent.width,
                                     1.0f / (float)extent.height};

    if (!valid(p) || !command || !image || !framebuffer || !pass || !pipeline ||
        !pipeline->pipeline || !pipeline->layout || !vertices ||
        !vertices->buffer || !extent.width || !extent.height || !vertex_count)
        return false;
    if (p->reset_command_buffer(command, 0) != VK_SUCCESS ||
        p->begin_command_buffer(command, &begin) != VK_SUCCESS)
        return false;
    p->pipeline_barrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, 0,
                        0, 0, 1, &to_color);
    p->begin_render_pass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
    p->bind_pipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     pipeline->pipeline);
    p->set_viewport(command, 0, 1, &viewport);
    p->set_scissor(command, 0, 1, &scissor);
    p->push_constants(command, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                      sizeof(inverse_extent), inverse_extent);
    p->bind_vertex_buffers(command, 0, 1, &vertices->buffer, &offset);
    p->draw(command, vertex_count, 1, 0, 0);
    p->end_render_pass(command);
    to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    p->pipeline_barrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, 0, 0, 0, 1,
                        &to_present);
    return p->end_command_buffer(command) == VK_SUCCESS;
}
