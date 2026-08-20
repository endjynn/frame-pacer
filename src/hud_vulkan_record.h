#ifndef FRAME_PACER_HUD_VULKAN_RECORD_H
#define FRAME_PACER_HUD_VULKAN_RECORD_H

#include "hud_vulkan_draw_resources.h"
#include "hud_vulkan_pipeline.h"
#include "hud_vulkan_resources.h"
#include "hud_vulkan_vertex_buffer.h"
#include <stdbool.h>

struct frame_pacer_hud_record_provider {
    PFN_vkResetCommandBuffer reset_command_buffer;
    PFN_vkBeginCommandBuffer begin_command_buffer;
    PFN_vkEndCommandBuffer end_command_buffer;
    PFN_vkCmdPipelineBarrier pipeline_barrier;
    PFN_vkCmdBeginRenderPass begin_render_pass;
    PFN_vkCmdEndRenderPass end_render_pass;
    PFN_vkCmdBindPipeline bind_pipeline;
    PFN_vkCmdBindVertexBuffers bind_vertex_buffers;
    PFN_vkCmdDraw draw;
    PFN_vkCmdSetViewport set_viewport;
    PFN_vkCmdSetScissor set_scissor;
    PFN_vkCmdPushConstants push_constants;
};

bool frame_pacer_hud_record(struct frame_pacer_hud_record_provider const *,
    VkCommandBuffer, VkImage, VkFramebuffer, VkRenderPass,
    const struct frame_pacer_hud_pipeline *, const struct frame_pacer_hud_vertex_buffer *,
    VkExtent2D, uint32_t vertex_count);

#endif
