#ifndef FRAME_PACER_HUD_VULKAN_PIPELINE_H
#define FRAME_PACER_HUD_VULKAN_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <vulkan/vulkan.h>

struct frame_pacer_hud_pipeline_provider {
    PFN_vkCreateShaderModule create_shader_module;
    PFN_vkDestroyShaderModule destroy_shader_module;
    PFN_vkCreatePipelineLayout create_pipeline_layout;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines;
    PFN_vkDestroyPipeline destroy_pipeline;
};

struct frame_pacer_hud_pipeline {
    VkPipelineLayout layout;
    VkPipeline pipeline;
};

bool frame_pacer_hud_create_pipeline(
    struct frame_pacer_hud_pipeline *,
    const struct frame_pacer_hud_pipeline_provider *, VkDevice, VkRenderPass,
    const uint32_t *, size_t, const uint32_t *, size_t);
void frame_pacer_hud_destroy_pipeline(
    struct frame_pacer_hud_pipeline *,
    const struct frame_pacer_hud_pipeline_provider *, VkDevice,
    const VkAllocationCallbacks *);

#endif
