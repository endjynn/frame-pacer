#include "hud_vulkan_commands.h"

#include <string.h>

static const char *const required_commands[] = {
    [FRAME_PACER_HUD_COMMAND_GET_SWAPCHAIN_IMAGES] = "vkGetSwapchainImagesKHR",
    [FRAME_PACER_HUD_COMMAND_CREATE_IMAGE_VIEW] = "vkCreateImageView",
    [FRAME_PACER_HUD_COMMAND_DESTROY_IMAGE_VIEW] = "vkDestroyImageView",
    [FRAME_PACER_HUD_COMMAND_CREATE_RENDER_PASS] = "vkCreateRenderPass",
    [FRAME_PACER_HUD_COMMAND_DESTROY_RENDER_PASS] = "vkDestroyRenderPass",
    [FRAME_PACER_HUD_COMMAND_CREATE_FRAMEBUFFER] = "vkCreateFramebuffer",
    [FRAME_PACER_HUD_COMMAND_DESTROY_FRAMEBUFFER] = "vkDestroyFramebuffer",
    [FRAME_PACER_HUD_COMMAND_CREATE_SHADER_MODULE] = "vkCreateShaderModule",
    [FRAME_PACER_HUD_COMMAND_DESTROY_SHADER_MODULE] = "vkDestroyShaderModule",
    [FRAME_PACER_HUD_COMMAND_CREATE_PIPELINE_LAYOUT] = "vkCreatePipelineLayout",
    [FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE_LAYOUT] =
        "vkDestroyPipelineLayout",
    [FRAME_PACER_HUD_COMMAND_CREATE_GRAPHICS_PIPELINES] =
        "vkCreateGraphicsPipelines",
    [FRAME_PACER_HUD_COMMAND_DESTROY_PIPELINE] = "vkDestroyPipeline",
    [FRAME_PACER_HUD_COMMAND_CREATE_COMMAND_POOL] = "vkCreateCommandPool",
    [FRAME_PACER_HUD_COMMAND_DESTROY_COMMAND_POOL] = "vkDestroyCommandPool",
    [FRAME_PACER_HUD_COMMAND_ALLOCATE_COMMAND_BUFFERS] =
        "vkAllocateCommandBuffers",
    [FRAME_PACER_HUD_COMMAND_CREATE_FENCE] = "vkCreateFence",
    [FRAME_PACER_HUD_COMMAND_DESTROY_FENCE] = "vkDestroyFence",
    [FRAME_PACER_HUD_COMMAND_WAIT_FOR_FENCES] = "vkWaitForFences",
    [FRAME_PACER_HUD_COMMAND_RESET_FENCES] = "vkResetFences",
    [FRAME_PACER_HUD_COMMAND_CREATE_SEMAPHORE] = "vkCreateSemaphore",
    [FRAME_PACER_HUD_COMMAND_DESTROY_SEMAPHORE] = "vkDestroySemaphore",
    [FRAME_PACER_HUD_COMMAND_CREATE_BUFFER] = "vkCreateBuffer",
    [FRAME_PACER_HUD_COMMAND_DESTROY_BUFFER] = "vkDestroyBuffer",
    [FRAME_PACER_HUD_COMMAND_ALLOCATE_MEMORY] = "vkAllocateMemory",
    [FRAME_PACER_HUD_COMMAND_FREE_MEMORY] = "vkFreeMemory",
    [FRAME_PACER_HUD_COMMAND_MAP_MEMORY] = "vkMapMemory",
    [FRAME_PACER_HUD_COMMAND_UNMAP_MEMORY] = "vkUnmapMemory",
    [FRAME_PACER_HUD_COMMAND_BIND_BUFFER_MEMORY] = "vkBindBufferMemory",
    [FRAME_PACER_HUD_COMMAND_GET_BUFFER_MEMORY_REQUIREMENTS] =
        "vkGetBufferMemoryRequirements",
    [FRAME_PACER_HUD_COMMAND_RESET_COMMAND_BUFFER] = "vkResetCommandBuffer",
    [FRAME_PACER_HUD_COMMAND_BEGIN_COMMAND_BUFFER] = "vkBeginCommandBuffer",
    [FRAME_PACER_HUD_COMMAND_END_COMMAND_BUFFER] = "vkEndCommandBuffer",
    [FRAME_PACER_HUD_COMMAND_PIPELINE_BARRIER] = "vkCmdPipelineBarrier",
    [FRAME_PACER_HUD_COMMAND_BEGIN_RENDER_PASS] = "vkCmdBeginRenderPass",
    [FRAME_PACER_HUD_COMMAND_END_RENDER_PASS] = "vkCmdEndRenderPass",
    [FRAME_PACER_HUD_COMMAND_BIND_PIPELINE] = "vkCmdBindPipeline",
    [FRAME_PACER_HUD_COMMAND_BIND_VERTEX_BUFFERS] = "vkCmdBindVertexBuffers",
    [FRAME_PACER_HUD_COMMAND_DRAW] = "vkCmdDraw",
    [FRAME_PACER_HUD_COMMAND_QUEUE_SUBMIT] = "vkQueueSubmit",
    [FRAME_PACER_HUD_COMMAND_SET_VIEWPORT] = "vkCmdSetViewport",
    [FRAME_PACER_HUD_COMMAND_SET_SCISSOR] = "vkCmdSetScissor",
    [FRAME_PACER_HUD_COMMAND_PUSH_CONSTANTS] = "vkCmdPushConstants",
};

_Static_assert(sizeof(required_commands) / sizeof(required_commands[0]) ==
                   FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT,
               "fixed command contract");

const char *frame_pacer_hud_required_command_name(unsigned int index)
{
    return index < FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT
               ? required_commands[index]
               : 0;
}

bool frame_pacer_hud_resolve_commands(struct frame_pacer_hud_commands *commands,
                                      PFN_vkGetDeviceProcAddr get_proc,
                                      VkDevice device)
{
    unsigned int index;

    if (!commands || !get_proc)
        return false;

    memset(commands, 0, sizeof(*commands));
    for (index = 0; index < FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT; ++index) {
        commands->functions[index] = get_proc(device, required_commands[index]);
        if (!commands->functions[index]) {
            memset(commands, 0, sizeof(*commands));
            return false;
        }
    }
    return true;
}
