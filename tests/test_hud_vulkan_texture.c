#include "hud_vulkan_texture.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static unsigned int calls, fail_at, next_handle, live_count, barriers, copies;
static bool live[32];
static unsigned char mapped[65536];
static const struct frame_pacer_font_atlas *atlas;

static VkResult step(void)
{
    return ++calls == fail_at ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}

static uintptr_t acquire(void)
{
    assert(next_handle + 1 < 32);
    live[++next_handle] = true;
    ++live_count;
    return next_handle;
}

static void release(uintptr_t handle)
{
    assert(handle && handle < 32 && live[handle]);
    live[handle] = false;
    --live_count;
}

#define CREATE(name, type, info_type, check)                                   \
    static VkResult VKAPI_CALL name(VkDevice device, const info_type *info,    \
                                    const VkAllocationCallbacks *allocator,    \
                                    type *out)                                 \
    {                                                                          \
        (void)device;                                                          \
        (void)allocator;                                                       \
        assert(check);                                                         \
        VkResult result = step();                                              \
        if (result == VK_SUCCESS)                                              \
            *out = (type)acquire();                                            \
        return result;                                                         \
    }
#define DESTROY(name, type)                                                    \
    static void VKAPI_CALL name(VkDevice device, type object,                  \
                                const VkAllocationCallbacks *allocator)        \
    {                                                                          \
        (void)device;                                                          \
        (void)allocator;                                                       \
        release((uintptr_t)object);                                            \
    }

CREATE(create_image, VkImage, VkImageCreateInfo,
       info->format == VK_FORMAT_R8_UNORM &&
           info->tiling == VK_IMAGE_TILING_OPTIMAL &&
           info->extent.width == atlas->width &&
           info->extent.height == atlas->height &&
           info->usage ==
               (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
CREATE(create_view, VkImageView, VkImageViewCreateInfo,
       info->image && info->format == VK_FORMAT_R8_UNORM &&
           info->subresourceRange.levelCount == 1)
CREATE(create_sampler, VkSampler, VkSamplerCreateInfo,
       info->minFilter == VK_FILTER_NEAREST &&
           info->magFilter == VK_FILTER_NEAREST &&
           info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
           !info->anisotropyEnable)
CREATE(create_layout, VkDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo,
       info->bindingCount == 1 && info->pBindings[0].binding == 0 &&
           info->pBindings[0].descriptorType ==
               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           info->pBindings[0].stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT)
CREATE(create_pool, VkDescriptorPool, VkDescriptorPoolCreateInfo,
       info->maxSets == 1 && info->poolSizeCount == 1 &&
           info->pPoolSizes[0].descriptorCount == 1)
CREATE(create_buffer, VkBuffer, VkBufferCreateInfo,
       info->size == (VkDeviceSize)atlas->width * atlas->height &&
           info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
CREATE(allocate_memory, VkDeviceMemory, VkMemoryAllocateInfo,
       info->allocationSize == sizeof(mapped) && info->memoryTypeIndex < 2)
DESTROY(destroy_image, VkImage)
DESTROY(destroy_view, VkImageView)
DESTROY(destroy_sampler, VkSampler)
DESTROY(destroy_layout, VkDescriptorSetLayout)
DESTROY(destroy_pool, VkDescriptorPool)
DESTROY(destroy_buffer, VkBuffer)
DESTROY(free_memory, VkDeviceMemory)

static void VKAPI_CALL image_requirements(VkDevice device, VkImage image,
                                          VkMemoryRequirements *out)
{
    (void)device;
    assert(image);
    *out = (VkMemoryRequirements){sizeof(mapped), 256, 1};
}
static void VKAPI_CALL buffer_requirements(VkDevice device, VkBuffer buffer,
                                           VkMemoryRequirements *out)
{
    (void)device;
    assert(buffer);
    *out = (VkMemoryRequirements){sizeof(mapped), 256, 2};
}
static VkResult VKAPI_CALL bind_image(VkDevice device, VkImage image,
                                      VkDeviceMemory memory,
                                      VkDeviceSize offset)
{
    (void)device;
    assert(image && memory && offset == 0);
    return step();
}
static VkResult VKAPI_CALL bind_buffer(VkDevice device, VkBuffer buffer,
                                       VkDeviceMemory memory,
                                       VkDeviceSize offset)
{
    (void)device;
    assert(buffer && memory && offset == 0);
    return step();
}
static VkResult VKAPI_CALL
allocate_sets(VkDevice device, const VkDescriptorSetAllocateInfo *info,
              VkDescriptorSet *out)
{
    (void)device;
    assert(info->descriptorSetCount == 1 && info->descriptorPool &&
           info->pSetLayouts[0]);
    VkResult result = step();
    if (result == VK_SUCCESS)
        *out = (VkDescriptorSet)(uintptr_t)31; /* Owned by the pool. */
    return result;
}
static void VKAPI_CALL update_sets(VkDevice device, uint32_t count,
                                   const VkWriteDescriptorSet *writes,
                                   uint32_t copy_count,
                                   const VkCopyDescriptorSet *copy)
{
    (void)device;
    (void)copy;
    assert(count == 1 && copy_count == 0 && writes->dstBinding == 0);
    assert(writes->pImageInfo->sampler && writes->pImageInfo->imageView);
    assert(writes->pImageInfo->imageLayout ==
           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
static VkResult VKAPI_CALL map_memory(VkDevice device, VkDeviceMemory memory,
                                      VkDeviceSize offset, VkDeviceSize size,
                                      VkMemoryMapFlags flags, void **out)
{
    (void)device;
    assert(memory && offset == 0 && !flags && size <= sizeof(mapped));
    VkResult result = step();
    if (result == VK_SUCCESS)
        *out = mapped;
    return result;
}
static void VKAPI_CALL unmap_memory(VkDevice device, VkDeviceMemory memory)
{
    (void)device;
    assert(memory);
    assert(!memcmp(mapped, frame_pacer_font_atlas_pixels(atlas),
                   (size_t)atlas->width * atlas->height));
}
static void VKAPI_CALL
barrier(VkCommandBuffer command, VkPipelineStageFlags src,
        VkPipelineStageFlags dst, VkDependencyFlags flags,
        uint32_t memory_count, const VkMemoryBarrier *memory,
        uint32_t buffer_count, const VkBufferMemoryBarrier *buffer,
        uint32_t image_count, const VkImageMemoryBarrier *image)
{
    (void)memory;
    (void)buffer;
    assert(command && !flags && !memory_count && !buffer_count &&
           image_count == 1);
    assert(image->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
           image->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    if (barriers == 0) {
        assert(src == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
               dst == VK_PIPELINE_STAGE_TRANSFER_BIT);
        assert(image->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               image->newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        assert(image->srcAccessMask == 0 &&
               image->dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
    } else {
        assert(barriers == 1 && copies == 1);
        assert(src == VK_PIPELINE_STAGE_TRANSFER_BIT &&
               dst == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        assert(image->oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               image->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        assert(image->srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT &&
               image->dstAccessMask == VK_ACCESS_SHADER_READ_BIT);
    }
    ++barriers;
}
static void VKAPI_CALL copy_image(VkCommandBuffer command, VkBuffer buffer,
                                  VkImage image, VkImageLayout layout,
                                  uint32_t count, const VkBufferImageCopy *copy)
{
    assert(command && buffer && image &&
           layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1);
    assert(barriers == 1 && !copies);
    assert(!copy->bufferOffset && !copy->bufferRowLength &&
           !copy->bufferImageHeight);
    assert(copy->imageExtent.width == atlas->width &&
           copy->imageExtent.height == atlas->height);
    ++copies;
}
static void VKAPI_CALL unused(void)
{
    assert(0);
}

int main(void)
{
    struct frame_pacer_hud_commands commands;
    for (unsigned int i = 0; i < FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT; ++i)
        commands.functions[i] = unused;
#define SET(name, function)                                                    \
    commands.functions[FRAME_PACER_HUD_COMMAND_##name] =                       \
        (PFN_vkVoidFunction)function
    SET(CREATE_IMAGE, create_image);
    SET(DESTROY_IMAGE, destroy_image);
    SET(CREATE_IMAGE_VIEW, create_view);
    SET(DESTROY_IMAGE_VIEW, destroy_view);
    SET(CREATE_SAMPLER, create_sampler);
    SET(DESTROY_SAMPLER, destroy_sampler);
    SET(CREATE_DESCRIPTOR_SET_LAYOUT, create_layout);
    SET(DESTROY_DESCRIPTOR_SET_LAYOUT, destroy_layout);
    SET(CREATE_DESCRIPTOR_POOL, create_pool);
    SET(DESTROY_DESCRIPTOR_POOL, destroy_pool);
    SET(CREATE_BUFFER, create_buffer);
    SET(DESTROY_BUFFER, destroy_buffer);
    SET(ALLOCATE_MEMORY, allocate_memory);
    SET(FREE_MEMORY, free_memory);
    SET(GET_IMAGE_MEMORY_REQUIREMENTS, image_requirements);
    SET(GET_BUFFER_MEMORY_REQUIREMENTS, buffer_requirements);
    SET(BIND_IMAGE_MEMORY, bind_image);
    SET(BIND_BUFFER_MEMORY, bind_buffer);
    SET(ALLOCATE_DESCRIPTOR_SETS, allocate_sets);
    SET(UPDATE_DESCRIPTOR_SETS, update_sets);
    SET(MAP_MEMORY, map_memory);
    SET(UNMAP_MEMORY, unmap_memory);
    SET(PIPELINE_BARRIER, barrier);
    SET(COPY_BUFFER_TO_IMAGE, copy_image);
#undef SET
    VkPhysicalDeviceMemoryProperties properties = {
        .memoryTypeCount = 2,
        .memoryTypes = {{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0},
                        {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         0}}};
    VkDevice device = (VkDevice)(uintptr_t)1;
    atlas = frame_pacer_font_atlas_at_size(24);
    struct frame_pacer_hud_texture texture;
    assert(frame_pacer_hud_create_texture(&texture, &commands, device,
                                          &properties, atlas));
    unsigned int total_calls = calls;
    assert(total_calls == 12 && live_count == 8);
    assert(texture.allocation_bytes == 2 * sizeof(mapped) && !texture.uploaded);
    frame_pacer_hud_record_texture_upload(&texture, &commands,
                                          (VkCommandBuffer)(uintptr_t)1);
    assert(barriers == 2 && copies == 1 && !texture.uploaded);
    texture.uploaded = true;
    frame_pacer_hud_record_texture_upload(&texture, &commands,
                                          (VkCommandBuffer)(uintptr_t)1);
    assert(barriers == 2 && copies == 1);
    frame_pacer_hud_destroy_texture(&texture, &commands, device);
    assert(!live_count);
    for (fail_at = 1; fail_at <= total_calls; ++fail_at) {
        calls = next_handle = 0;
        assert(!frame_pacer_hud_create_texture(&texture, &commands, device,
                                               &properties, atlas));
        assert(calls == fail_at && live_count == 0);
        assert(!texture.image && !texture.staging && !texture.descriptor &&
               !texture.atlas);
        frame_pacer_hud_destroy_texture(&texture, &commands, device);
    }
    fail_at = 0;
    for (unsigned int missing = 0; missing < 2; ++missing) {
        VkPhysicalDeviceMemoryProperties unavailable = properties;
        unavailable.memoryTypes[missing].propertyFlags = 0;
        calls = next_handle = 0;
        assert(!frame_pacer_hud_create_texture(&texture, &commands, device,
                                               &unavailable, atlas));
        assert(!live_count);
    }
    return 0;
}
