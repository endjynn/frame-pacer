#ifndef FRAME_PACER_HUD_VULKAN_VERTEX_BUFFER_H
#define FRAME_PACER_HUD_VULKAN_VERTEX_BUFFER_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

struct frame_pacer_hud_vertex_buffer_provider {
    PFN_vkCreateBuffer create_buffer;
    PFN_vkDestroyBuffer destroy_buffer;
    PFN_vkGetBufferMemoryRequirements get_requirements;
    PFN_vkAllocateMemory allocate_memory;
    PFN_vkFreeMemory free_memory;
    PFN_vkBindBufferMemory bind_memory;
    PFN_vkMapMemory map_memory;
    PFN_vkUnmapMemory unmap_memory;
};

struct frame_pacer_hud_vertex_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *map;
    VkDeviceSize size;
};

bool frame_pacer_hud_create_vertex_buffer(
    struct frame_pacer_hud_vertex_buffer *,
    const struct frame_pacer_hud_vertex_buffer_provider *, VkDevice,
    const VkPhysicalDeviceMemoryProperties *, VkDeviceSize);
void frame_pacer_hud_destroy_vertex_buffer(
    struct frame_pacer_hud_vertex_buffer *,
    const struct frame_pacer_hud_vertex_buffer_provider *, VkDevice,
    const VkAllocationCallbacks *);

#endif
