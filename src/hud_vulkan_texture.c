#include "hud_vulkan_texture.h"

#include <string.h>

#define FN(name, type)                                                         \
    ((PFN_##type)commands->functions[FRAME_PACER_HUD_COMMAND_##name])

static bool allocate(const struct frame_pacer_hud_commands *commands,
                     VkDevice device,
                     const VkPhysicalDeviceMemoryProperties *properties,
                     const VkMemoryRequirements *requirements,
                     VkMemoryPropertyFlags flags, VkDeviceMemory *memory)
{
    for (uint32_t i = 0; i < properties->memoryTypeCount; ++i) {
        if (!(requirements->memoryTypeBits & (1U << i)) ||
            (properties->memoryTypes[i].propertyFlags & flags) != flags)
            continue;
        VkMemoryAllocateInfo info = {.sType =
                                         VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = requirements->size,
                                     .memoryTypeIndex = i};
        return FN(ALLOCATE_MEMORY, vkAllocateMemory)(device, &info, NULL,
                                                     memory) == VK_SUCCESS;
    }
    return false;
}

void frame_pacer_hud_destroy_texture(
    struct frame_pacer_hud_texture *texture,
    const struct frame_pacer_hud_commands *commands, VkDevice device)
{
    if (!texture || !commands)
        return;
    if (texture->pool)
        FN(DESTROY_DESCRIPTOR_POOL,
           vkDestroyDescriptorPool)(device, texture->pool, NULL);
    if (texture->layout)
        FN(DESTROY_DESCRIPTOR_SET_LAYOUT,
           vkDestroyDescriptorSetLayout)(device, texture->layout, NULL);
    if (texture->sampler)
        FN(DESTROY_SAMPLER, vkDestroySampler)(device, texture->sampler, NULL);
    if (texture->view)
        FN(DESTROY_IMAGE_VIEW, vkDestroyImageView)(device, texture->view, NULL);
    if (texture->image)
        FN(DESTROY_IMAGE, vkDestroyImage)(device, texture->image, NULL);
    if (texture->image_memory)
        FN(FREE_MEMORY, vkFreeMemory)(device, texture->image_memory, NULL);
    if (texture->staging)
        FN(DESTROY_BUFFER, vkDestroyBuffer)(device, texture->staging, NULL);
    if (texture->staging_memory)
        FN(FREE_MEMORY, vkFreeMemory)(device, texture->staging_memory, NULL);
    memset(texture, 0, sizeof(*texture));
}

bool frame_pacer_hud_create_texture(
    struct frame_pacer_hud_texture *texture,
    const struct frame_pacer_hud_commands *commands, VkDevice device,
    const VkPhysicalDeviceMemoryProperties *properties,
    const struct frame_pacer_font_atlas *atlas)
{
    if (!texture || !commands || !device || !properties || !atlas)
        return false;
    memset(texture, 0, sizeof(*texture));
    for (unsigned int i = 0; i < FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT; ++i)
        if (!commands->functions[i])
            return false;
    const VkImageCreateInfo image = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {atlas->width, atlas->height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkMemoryRequirements requirements;
    if (FN(CREATE_IMAGE, vkCreateImage)(device, &image, NULL,
                                        &texture->image) != VK_SUCCESS)
        goto fail;
    FN(GET_IMAGE_MEMORY_REQUIREMENTS,
       vkGetImageMemoryRequirements)(device, texture->image, &requirements);
    if (!allocate(commands, device, properties, &requirements,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  &texture->image_memory) ||
        FN(BIND_IMAGE_MEMORY, vkBindImageMemory)(
            device, texture->image, texture->image_memory, 0) != VK_SUCCESS)
        goto fail;
    texture->allocation_bytes = requirements.size;
    const VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1}};
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (FN(CREATE_IMAGE_VIEW, vkCreateImageView)(
            device, &view, NULL, &texture->view) != VK_SUCCESS ||
        FN(CREATE_SAMPLER, vkCreateSampler)(device, &sampler, NULL,
                                            &texture->sampler) != VK_SUCCESS)
        goto fail;
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    const VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding};
    const VkDescriptorPoolSize pool_size = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size};
    if (FN(CREATE_DESCRIPTOR_SET_LAYOUT, vkCreateDescriptorSetLayout)(
            device, &layout, NULL, &texture->layout) != VK_SUCCESS ||
        FN(CREATE_DESCRIPTOR_POOL, vkCreateDescriptorPool)(
            device, &pool, NULL, &texture->pool) != VK_SUCCESS)
        goto fail;
    const VkDescriptorSetAllocateInfo set = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = texture->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &texture->layout};
    if (FN(ALLOCATE_DESCRIPTOR_SETS, vkAllocateDescriptorSets)(
            device, &set, &texture->descriptor) != VK_SUCCESS)
        goto fail;
    const VkDescriptorImageInfo descriptor = {
        texture->sampler, texture->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture->descriptor,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor};
    FN(UPDATE_DESCRIPTOR_SETS, vkUpdateDescriptorSets)(device, 1, &write, 0,
                                                       NULL);
    const VkBufferCreateInfo buffer = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)atlas->width * atlas->height,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (FN(CREATE_BUFFER, vkCreateBuffer)(device, &buffer, NULL,
                                          &texture->staging) != VK_SUCCESS)
        goto fail;
    FN(GET_BUFFER_MEMORY_REQUIREMENTS,
       vkGetBufferMemoryRequirements)(device, texture->staging, &requirements);
    if (!allocate(commands, device, properties, &requirements,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &texture->staging_memory) ||
        FN(BIND_BUFFER_MEMORY, vkBindBufferMemory)(
            device, texture->staging, texture->staging_memory, 0) != VK_SUCCESS)
        goto fail;
    texture->allocation_bytes += requirements.size;
    void *map = NULL;
    if (FN(MAP_MEMORY, vkMapMemory)(device, texture->staging_memory, 0,
                                    buffer.size, 0, &map) != VK_SUCCESS)
        goto fail;
    memcpy(map, frame_pacer_font_atlas_pixels(atlas), (size_t)buffer.size);
    FN(UNMAP_MEMORY, vkUnmapMemory)(device, texture->staging_memory);
    texture->atlas = atlas;
    return true;
fail:
    frame_pacer_hud_destroy_texture(texture, commands, device);
    return false;
}

void frame_pacer_hud_record_texture_upload(
    const struct frame_pacer_hud_texture *texture,
    const struct frame_pacer_hud_commands *commands, VkCommandBuffer command)
{
    if (texture->uploaded)
        return;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1}};
    FN(PIPELINE_BARRIER, vkCmdPipelineBarrier)(
        command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    const VkBufferImageCopy copy = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .layerCount = 1},
        .imageExtent = {texture->atlas->width, texture->atlas->height, 1}};
    FN(COPY_BUFFER_TO_IMAGE,
       vkCmdCopyBufferToImage)(command, texture->staging, texture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    FN(PIPELINE_BARRIER,
       vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 1, &barrier);
}
