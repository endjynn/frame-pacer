#include "hud_vulkan_pipeline.h"

#include <assert.h>
#include <stdint.h>

static unsigned int calls;
static unsigned int destroys;
static unsigned int fail_at;

static VkResult next_result(void)
{
    return ++calls == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}

static VkResult VKAPI_CALL create_shader(
    VkDevice device, const VkShaderModuleCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkShaderModule *shader)
{
    (void)device;
    (void)allocator;
    assert(info->codeSize == sizeof(uint32_t));
    *shader = (VkShaderModule)(uintptr_t)(1 + calls);
    return next_result();
}

static VkResult VKAPI_CALL create_layout(
    VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *layout)
{
    (void)device;
    (void)allocator;
    assert(info->pushConstantRangeCount == 1);
    *layout = (VkPipelineLayout)(uintptr_t)7;
    return next_result();
}

static VkResult VKAPI_CALL create_pipeline(
    VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipeline *pipeline)
{
    (void)device;
    (void)cache;
    (void)allocator;
    assert(count == 1);
    assert(info->stageCount == 2);
    *pipeline = (VkPipeline)(uintptr_t)8;
    return next_result();
}

static void VKAPI_CALL destroy_shader(VkDevice device, VkShaderModule shader,
                                      const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)shader;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_layout(VkDevice device, VkPipelineLayout layout,
                                      const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)layout;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL destroy_pipeline(VkDevice device, VkPipeline pipeline,
                                        const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)pipeline;
    (void)allocator;
    ++destroys;
}

static const struct frame_pacer_hud_pipeline_provider provider = {
    .create_shader_module = create_shader,
    .destroy_shader_module = destroy_shader,
    .create_pipeline_layout = create_layout,
    .destroy_pipeline_layout = destroy_layout,
    .create_graphics_pipelines = create_pipeline,
    .destroy_pipeline = destroy_pipeline,
};

int main(void)
{
    const VkDevice device = (VkDevice)(uintptr_t)1;
    const VkRenderPass render_pass = (VkRenderPass)(uintptr_t)2;
    struct frame_pacer_hud_pipeline pipeline;
    uint32_t spv = 0;

    calls = destroys = fail_at = 0;
    assert(frame_pacer_hud_create_pipeline(
        &pipeline, &provider, device, render_pass, &spv, sizeof(spv), &spv,
        sizeof(spv)));
    frame_pacer_hud_destroy_pipeline(&pipeline, &provider, device, 0);
    assert(destroys == 4);

    calls = destroys = 0;
    fail_at = 3;
    assert(!frame_pacer_hud_create_pipeline(
        &pipeline, &provider, device, render_pass, &spv, sizeof(spv), &spv,
        sizeof(spv)));
    assert(destroys == 3);
    return 0;
}
