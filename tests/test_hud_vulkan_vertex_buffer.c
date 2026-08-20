#include "hud_vulkan_vertex_buffer.h"

#include <assert.h>
#include <stdint.h>

static unsigned int calls;
static unsigned int destroys;
static unsigned int fail_at;

static VkResult next_result(void)
{
    return ++calls == fail_at ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}

static VkResult VKAPI_CALL create_buffer(
    VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *buffer)
{
    (void)device;
    (void)info;
    (void)allocator;
    *buffer = (VkBuffer)(uintptr_t)1;
    return next_result();
}

static void VKAPI_CALL destroy_buffer(VkDevice device, VkBuffer buffer,
                                      const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)buffer;
    (void)allocator;
    ++destroys;
}

static void VKAPI_CALL get_requirements(VkDevice device, VkBuffer buffer,
                                        VkMemoryRequirements *requirements)
{
    (void)device;
    (void)buffer;
    *requirements = (VkMemoryRequirements){
        .size = 4096,
        .memoryTypeBits = 1,
    };
}

static VkResult VKAPI_CALL allocate_memory(
    VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *memory)
{
    (void)device;
    (void)info;
    (void)allocator;
    *memory = (VkDeviceMemory)(uintptr_t)2;
    return next_result();
}

static void VKAPI_CALL free_memory(VkDevice device, VkDeviceMemory memory,
                                   const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)memory;
    (void)allocator;
    ++destroys;
}

static VkResult VKAPI_CALL bind_memory(VkDevice device, VkBuffer buffer,
                                       VkDeviceMemory memory,
                                       VkDeviceSize offset)
{
    (void)device;
    (void)buffer;
    (void)memory;
    (void)offset;
    return next_result();
}

static VkResult VKAPI_CALL map_memory(VkDevice device, VkDeviceMemory memory,
                                      VkDeviceSize offset, VkDeviceSize size,
                                      VkMemoryMapFlags flags, void **mapping)
{
    static char mapped_byte;

    (void)device;
    (void)memory;
    (void)offset;
    (void)size;
    (void)flags;
    *mapping = &mapped_byte;
    return next_result();
}

static void VKAPI_CALL unmap_memory(VkDevice device, VkDeviceMemory memory)
{
    (void)device;
    (void)memory;
    ++destroys;
}

static const struct frame_pacer_hud_vertex_buffer_provider provider = {
    .create_buffer = create_buffer,
    .destroy_buffer = destroy_buffer,
    .get_requirements = get_requirements,
    .allocate_memory = allocate_memory,
    .free_memory = free_memory,
    .bind_memory = bind_memory,
    .map_memory = map_memory,
    .unmap_memory = unmap_memory,
};

int main(void)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {
        .memoryTypeCount = 1,
        .memoryTypes[0].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    const VkDevice device = (VkDevice)(uintptr_t)1;
    struct frame_pacer_hud_vertex_buffer buffer;

    calls = destroys = fail_at = 0;
    assert(frame_pacer_hud_create_vertex_buffer(
        &buffer, &provider, device, &memory_properties, 128));
    frame_pacer_hud_destroy_vertex_buffer(&buffer, &provider, device, 0);
    assert(destroys == 3);

    calls = destroys = 0;
    fail_at = 3;
    assert(!frame_pacer_hud_create_vertex_buffer(
        &buffer, &provider, device, &memory_properties, 128));
    assert(destroys == 2);

    calls = destroys = 0;
    fail_at = 4;
    assert(!frame_pacer_hud_create_vertex_buffer(
        &buffer, &provider, device, &memory_properties, 128));
    assert(destroys == 3);

    memory_properties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    calls = destroys = fail_at = 0;
    assert(!frame_pacer_hud_create_vertex_buffer(
        &buffer, &provider, device, &memory_properties, 128));
    assert(destroys == 1);
    return 0;
}
