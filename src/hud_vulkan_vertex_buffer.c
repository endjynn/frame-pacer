#include "hud_vulkan_vertex_buffer.h"

#include <string.h>

static bool
valid_provider(const struct frame_pacer_hud_vertex_buffer_provider *provider)
{
    return provider && provider->create_buffer && provider->destroy_buffer &&
           provider->get_requirements && provider->allocate_memory &&
           provider->free_memory && provider->bind_memory &&
           provider->map_memory && provider->unmap_memory;
}

void frame_pacer_hud_destroy_vertex_buffer(
    struct frame_pacer_hud_vertex_buffer *resources,
    const struct frame_pacer_hud_vertex_buffer_provider *provider,
    VkDevice device, const VkAllocationCallbacks *allocator)
{
    if (!resources)
        return;
    if (valid_provider(provider) && resources->map)
        provider->unmap_memory(device, resources->memory);
    if (valid_provider(provider) && resources->buffer)
        provider->destroy_buffer(device, resources->buffer, allocator);
    if (valid_provider(provider) && resources->memory)
        provider->free_memory(device, resources->memory, allocator);
    memset(resources, 0, sizeof(*resources));
}

bool frame_pacer_hud_create_vertex_buffer(
    struct frame_pacer_hud_vertex_buffer *resources,
    const struct frame_pacer_hud_vertex_buffer_provider *provider,
    VkDevice device, const VkPhysicalDeviceMemoryProperties *memory_properties,
    VkDeviceSize size)
{
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    };
    uint32_t memory_type = UINT32_MAX;
    uint32_t index;

    if (!resources || !memory_properties || !size || !valid_provider(provider))
        return false;

    memset(resources, 0, sizeof(*resources));
    if (provider->create_buffer(device, &buffer_info, 0, &resources->buffer) !=
            VK_SUCCESS ||
        !resources->buffer)
        goto fail;

    provider->get_requirements(device, resources->buffer, &requirements);
    for (index = 0; index < memory_properties->memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        if ((requirements.memoryTypeBits & (1U << index)) &&
            (memory_properties->memoryTypes[index].propertyFlags & required) ==
                required) {
            memory_type = index;
            break;
        }
    }
    if (memory_type == UINT32_MAX)
        goto fail;

    allocation_info.allocationSize = requirements.size;
    allocation_info.memoryTypeIndex = memory_type;
    if (provider->allocate_memory(device, &allocation_info, 0,
                                  &resources->memory) != VK_SUCCESS ||
        !resources->memory)
        goto fail;
    if (provider->bind_memory(device, resources->buffer, resources->memory,
                              0) != VK_SUCCESS)
        goto fail;
    if (provider->map_memory(device, resources->memory, 0, VK_WHOLE_SIZE, 0,
                             &resources->map) != VK_SUCCESS ||
        !resources->map)
        goto fail;

    resources->size = requirements.size;
    return true;

fail:
    frame_pacer_hud_destroy_vertex_buffer(resources, provider, device, 0);
    return false;
}
