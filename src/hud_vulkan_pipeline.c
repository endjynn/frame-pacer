#include "hud_vulkan_pipeline.h"

#include "hud_vertices.h"

#include <stddef.h>
#include <string.h>

static bool
valid_provider(const struct frame_pacer_hud_pipeline_provider *provider)
{
    return provider && provider->create_shader_module &&
           provider->destroy_shader_module &&
           provider->create_pipeline_layout &&
           provider->destroy_pipeline_layout &&
           provider->create_graphics_pipelines && provider->destroy_pipeline;
}

void frame_pacer_hud_destroy_pipeline(
    struct frame_pacer_hud_pipeline *resources,
    const struct frame_pacer_hud_pipeline_provider *provider, VkDevice device,
    const VkAllocationCallbacks *allocator)
{
    if (!resources)
        return;
    if (valid_provider(provider) && resources->pipeline)
        provider->destroy_pipeline(device, resources->pipeline, allocator);
    if (valid_provider(provider) && resources->layout)
        provider->destroy_pipeline_layout(device, resources->layout, allocator);
    memset(resources, 0, sizeof(*resources));
}

bool frame_pacer_hud_create_pipeline(
    struct frame_pacer_hud_pipeline *resources,
    const struct frame_pacer_hud_pipeline_provider *provider, VkDevice device,
    VkRenderPass render_pass, const uint32_t *vertex_code,
    size_t vertex_code_size, const uint32_t *fragment_code,
    size_t fragment_code_size)
{
    const VkPushConstantRange push_constant = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = 2 * sizeof(float),
    };
    const VkShaderModuleCreateInfo vertex_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertex_code_size,
        .pCode = vertex_code,
    };
    const VkShaderModuleCreateInfo fragment_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fragment_code_size,
        .pCode = fragment_code,
    };
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
    };
    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pName = "main",
        },
    };
    const VkVertexInputBindingDescription binding = {
        .stride = sizeof(struct frame_pacer_hud_vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[2] = {
        {
            .location = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct frame_pacer_hud_vertex, position),
        },
        {
            .location = 1,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(struct frame_pacer_hud_vertex, color),
        },
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
        .pDynamicStates = dynamic_states,
    };
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = sizeof(stages) / sizeof(stages[0]),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .renderPass = render_pass,
    };
    VkShaderModule vertex_module = VK_NULL_HANDLE;
    VkShaderModule fragment_module = VK_NULL_HANDLE;

    if (!resources || !valid_provider(provider) || !render_pass ||
        !vertex_code || !fragment_code || !vertex_code_size ||
        !fragment_code_size)
        return false;

    memset(resources, 0, sizeof(*resources));
    if (provider->create_shader_module(device, &vertex_module_info, 0,
                                       &vertex_module) != VK_SUCCESS ||
        provider->create_shader_module(device, &fragment_module_info, 0,
                                       &fragment_module) != VK_SUCCESS ||
        provider->create_pipeline_layout(device, &layout_info, 0,
                                         &resources->layout) != VK_SUCCESS)
        goto fail;

    stages[0].module = vertex_module;
    stages[1].module = fragment_module;
    pipeline_info.layout = resources->layout;
    if (provider->create_graphics_pipelines(device, VK_NULL_HANDLE, 1,
                                            &pipeline_info, 0,
                                            &resources->pipeline) != VK_SUCCESS)
        goto fail;

    provider->destroy_shader_module(device, fragment_module, 0);
    provider->destroy_shader_module(device, vertex_module, 0);
    return true;

fail:
    if (fragment_module)
        provider->destroy_shader_module(device, fragment_module, 0);
    if (vertex_module)
        provider->destroy_shader_module(device, vertex_module, 0);
    frame_pacer_hud_destroy_pipeline(resources, provider, device, 0);
    return false;
}
