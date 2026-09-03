#include "hud_vulkan_record.h"

#include <assert.h>
#include <stdint.h>

static unsigned int calls;
static unsigned int barriers;
static unsigned int draws;
static bool fail_begin;

static VkResult VKAPI_CALL reset_command_buffer(VkCommandBuffer command,
                                                VkCommandBufferResetFlags flags)
{
    (void)command;
    (void)flags;
    ++calls;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL begin_command_buffer(
    VkCommandBuffer command, const VkCommandBufferBeginInfo *info)
{
    (void)command;
    (void)info;
    ++calls;
    return fail_begin ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static VkResult VKAPI_CALL end_command_buffer(VkCommandBuffer command)
{
    (void)command;
    ++calls;
    return VK_SUCCESS;
}

static void VKAPI_CALL pipeline_barrier(
    VkCommandBuffer command, VkPipelineStageFlags source_stage,
    VkPipelineStageFlags destination_stage, VkDependencyFlags dependencies,
    uint32_t memory_barrier_count, const VkMemoryBarrier *memory_barriers,
    uint32_t buffer_barrier_count, const VkBufferMemoryBarrier *buffer_barriers,
    uint32_t image_barrier_count, const VkImageMemoryBarrier *image_barriers)
{
    (void)command;
    (void)source_stage;
    (void)destination_stage;
    (void)dependencies;
    (void)memory_barrier_count;
    (void)memory_barriers;
    (void)buffer_barrier_count;
    (void)buffer_barriers;
    assert(image_barrier_count == 1 && image_barriers);
    ++barriers;
}

static void VKAPI_CALL begin_render_pass(VkCommandBuffer command,
                                         const VkRenderPassBeginInfo *info,
                                         VkSubpassContents contents)
{
    (void)command;
    (void)info;
    (void)contents;
    ++calls;
}

static void VKAPI_CALL end_render_pass(VkCommandBuffer command)
{
    (void)command;
    ++calls;
}

static void VKAPI_CALL bind_pipeline(VkCommandBuffer command,
                                     VkPipelineBindPoint bind_point,
                                     VkPipeline pipeline)
{
    (void)command;
    (void)bind_point;
    (void)pipeline;
    ++calls;
}

static void VKAPI_CALL bind_vertex_buffers(VkCommandBuffer command,
                                           uint32_t first_binding,
                                           uint32_t binding_count,
                                           const VkBuffer *buffers,
                                           const VkDeviceSize *offsets)
{
    (void)command;
    (void)first_binding;
    (void)binding_count;
    (void)buffers;
    (void)offsets;
    ++calls;
}

static void VKAPI_CALL draw(VkCommandBuffer command, uint32_t vertex_count,
                            uint32_t instance_count, uint32_t first_vertex,
                            uint32_t first_instance)
{
    (void)command;
    (void)vertex_count;
    (void)instance_count;
    (void)first_vertex;
    (void)first_instance;
    ++draws;
}

static void VKAPI_CALL set_viewport(VkCommandBuffer command,
                                    uint32_t first_viewport,
                                    uint32_t viewport_count,
                                    const VkViewport *viewports)
{
    (void)command;
    (void)first_viewport;
    (void)viewport_count;
    (void)viewports;
    ++calls;
}

static void VKAPI_CALL set_scissor(VkCommandBuffer command,
                                   uint32_t first_scissor,
                                   uint32_t scissor_count,
                                   const VkRect2D *scissors)
{
    (void)command;
    (void)first_scissor;
    (void)scissor_count;
    (void)scissors;
    ++calls;
}

static void VKAPI_CALL push_constants(VkCommandBuffer command,
                                      VkPipelineLayout layout,
                                      VkShaderStageFlags stage_flags,
                                      uint32_t offset, uint32_t size,
                                      const void *values)
{
    (void)command;
    (void)layout;
    (void)stage_flags;
    (void)offset;
    (void)size;
    (void)values;
    ++calls;
}

static const struct frame_pacer_hud_record_provider provider = {
    .reset_command_buffer = reset_command_buffer,
    .begin_command_buffer = begin_command_buffer,
    .end_command_buffer = end_command_buffer,
    .pipeline_barrier = pipeline_barrier,
    .begin_render_pass = begin_render_pass,
    .end_render_pass = end_render_pass,
    .bind_pipeline = bind_pipeline,
    .bind_vertex_buffers = bind_vertex_buffers,
    .draw = draw,
    .set_viewport = set_viewport,
    .set_scissor = set_scissor,
    .push_constants = push_constants,
};

int main(void)
{
    const struct frame_pacer_hud_pipeline pipeline = {
        .layout = (VkPipelineLayout)(uintptr_t)1,
        .pipeline = (VkPipeline)(uintptr_t)2,
    };
    const struct frame_pacer_hud_vertex_buffer vertices = {
        .buffer = (VkBuffer)(uintptr_t)3,
    };
    const VkCommandBuffer command = (VkCommandBuffer)(uintptr_t)1;
    const VkImage image = (VkImage)(uintptr_t)2;
    const VkFramebuffer framebuffer = (VkFramebuffer)(uintptr_t)3;
    const VkRenderPass render_pass = (VkRenderPass)(uintptr_t)4;
    const VkExtent2D extent = {1280, 720};

    calls = barriers = draws = 0;
    fail_begin = false;
    assert(frame_pacer_hud_record(&provider, command, image, framebuffer,
                                  render_pass, &pipeline, &vertices, extent,
                                  6));
    assert(barriers == 2 && draws == 1);

    fail_begin = true;
    assert(!frame_pacer_hud_record(&provider, command, image, framebuffer,
                                   render_pass, &pipeline, &vertices, extent,
                                   6));
    return 0;
}
