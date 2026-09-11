#include "hud_spv.h"
#include "hud_vertices.h"
#include "hud_vulkan_record.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct context {
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceMemoryProperties memory;
    struct frame_pacer_hud_commands commands;
};

static VkDeviceMemory allocate(struct context *context,
                               VkMemoryRequirements requirements,
                               VkMemoryPropertyFlags flags)
{
    VkMemoryAllocateInfo info = {.sType =
                                     VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = requirements.size,
                                 .memoryTypeIndex = UINT32_MAX};
    for (uint32_t i = 0; i < context->memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (context->memory.memoryTypes[i].propertyFlags & flags) == flags) {
            info.memoryTypeIndex = i;
            break;
        }
    assert(info.memoryTypeIndex != UINT32_MAX);
    VkDeviceMemory memory;
    assert(vkAllocateMemory(context->device, &info, NULL, &memory) ==
           VK_SUCCESS);
    return memory;
}

/* Ordinary offscreen images have no presentation layout. Translate only that
 * layout at the driver boundary; execute the production record path unchanged.
 */
static void VKAPI_CALL
offscreen_barrier(VkCommandBuffer command, VkPipelineStageFlags src,
                  VkPipelineStageFlags dst, VkDependencyFlags flags,
                  uint32_t memory_count, const VkMemoryBarrier *memory,
                  uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                  uint32_t image_count, const VkImageMemoryBarrier *images)
{
    assert(image_count == 1);
    VkImageMemoryBarrier image = *images;
    if (image.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        image.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (image.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        image.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(command, src, dst, flags, memory_count, memory,
                         buffer_count, buffers, 1, &image);
}

static const struct frame_pacer_hud_pipeline_provider pipeline_provider = {
    vkCreateShaderModule,    vkDestroyShaderModule,     vkCreatePipelineLayout,
    vkDestroyPipelineLayout, vkCreateGraphicsPipelines, vkDestroyPipeline};
static const struct frame_pacer_hud_draw_provider draw_provider = {
    vkCreateRenderPass,       vkDestroyRenderPass, vkCreateFramebuffer,
    vkDestroyFramebuffer,     vkCreateCommandPool, vkDestroyCommandPool,
    vkAllocateCommandBuffers, vkCreateFence,       vkDestroyFence,
    vkCreateSemaphore,        vkDestroySemaphore};
static const struct frame_pacer_hud_vertex_buffer_provider vertex_provider = {
    vkCreateBuffer,   vkDestroyBuffer, vkGetBufferMemoryRequirements,
    vkAllocateMemory, vkFreeMemory,    vkBindBufferMemory,
    vkMapMemory,      vkUnmapMemory};
static const struct frame_pacer_hud_record_provider record_provider = {
    vkResetCommandBuffer, vkBeginCommandBuffer,   vkEndCommandBuffer,
    offscreen_barrier,    vkCmdBeginRenderPass,   vkCmdEndRenderPass,
    vkCmdBindPipeline,    vkCmdBindVertexBuffers, vkCmdDraw,
    vkCmdSetViewport,     vkCmdSetScissor,        vkCmdPushConstants};

static float decode(unsigned char value, bool srgb)
{
    float f = value / 255.0f;
    return !srgb           ? f
           : f <= 0.04045f ? f / 12.92f
                           : powf((f + 0.055f) / 1.055f, 2.4f);
}
static unsigned char encode(float value, bool srgb)
{
    if (srgb)
        value = value <= 0.0031308f
                    ? value * 12.92f
                    : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)(value * 255.0f + 0.5f);
}

static void check_pixels(struct context *context, VkExtent2D extent,
                         unsigned int rows, VkFormat format)
{
    struct frame_pacer_hud_text text = {{"GPU 100%  61\x7f", "CPU  83%  73\x7f",
                                         "THR  50%  50%",
                                         "FPS 999\x7e 999\x7e"},
                                        4};
    if (rows == 3) {
        memcpy(text.lines[2], text.lines[3], sizeof(text.lines[2]));
        text.line_count = 3;
    }
    struct frame_pacer_hud_vertices geometry;
    assert(frame_pacer_hud_vertices_build_for_extent(
        &geometry, &text, extent.width, extent.height));
    uint32_t width = (uint32_t)geometry.data[1].position[0];
    uint32_t height = (uint32_t)geometry.data[2].position[1];
    size_t bytes = (size_t)width * height * 4;
    bool srgb = format == VK_FORMAT_R8G8B8A8_SRGB;
    bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM;
    VkDevice device = context->device;
    VkImage image;
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    assert(vkCreateImage(device, &image_info, NULL, &image) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkDeviceMemory image_memory =
        allocate(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(vkBindImageMemory(device, image, image_memory, 0) == VK_SUCCESS);
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                           1};
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = range};
    VkImageView view;
    assert(vkCreateImageView(device, &view_info, NULL, &view) == VK_SUCCESS);
    struct frame_pacer_hud_image_views views = {.count = 1, .ready = true};
    views.images[0] = image;
    views.views[0] = view;
    struct frame_pacer_hud_draw_resources draw;
    assert(frame_pacer_hud_create_draw_resources(&draw, &draw_provider, device,
                                                 &views, format, extent,
                                                 context->family));
    struct frame_pacer_hud_texture texture;
    assert(frame_pacer_hud_create_texture(&texture, &context->commands, device,
                                          &context->memory, geometry.atlas));
    fprintf(stderr, "Font size=%u coverage=%u allocated=%llu bytes\n",
            geometry.atlas->pixel_size,
            (unsigned)geometry.atlas->width * geometry.atlas->height,
            (unsigned long long)texture.allocation_bytes);
    struct frame_pacer_hud_pipeline pipeline;
    assert(frame_pacer_hud_create_pipeline(
        &pipeline, &pipeline_provider, device, draw.render_pass, texture.layout,
        (const uint32_t *)build_shaders_hud_vert_spv,
        sizeof(build_shaders_hud_vert_spv),
        (const uint32_t *)build_shaders_hud_frag_spv,
        sizeof(build_shaders_hud_frag_spv)));
    struct frame_pacer_hud_vertex_buffer vertices;
    assert(frame_pacer_hud_create_vertex_buffer(&vertices, &vertex_provider,
                                                device, &context->memory,
                                                sizeof(geometry.data)));
    memcpy(vertices.map, geometry.data,
           geometry.count * sizeof(geometry.data[0]));
    const VkBufferCreateInfo read_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer read_buffer;
    assert(vkCreateBuffer(device, &read_info, NULL, &read_buffer) ==
           VK_SUCCESS);
    vkGetBufferMemoryRequirements(device, read_buffer, &requirements);
    VkDeviceMemory read_memory =
        allocate(context, requirements,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkBindBufferMemory(device, read_buffer, read_memory, 0) ==
           VK_SUCCESS);
    void *mapped;
    assert(vkMapMemory(device, read_memory, 0, bytes, 0, &mapped) ==
           VK_SUCCESS);
    VkCommandBuffer extra[2];
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = draw.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2};
    assert(vkAllocateCommandBuffers(device, &command_info, extra) ==
           VK_SUCCESS);
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    for (unsigned int repeat = 0; repeat < 2; ++repeat) {
        assert(vkResetCommandBuffer(extra[0], 0) == VK_SUCCESS);
        assert(vkBeginCommandBuffer(extra[0], &begin) == VK_SUCCESS);
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range};
        vkCmdPipelineBarrier(extra[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 1, &barrier);
        const VkClearColorValue clear = {
            .float32 = {0.25f, 0.375f, 0.5f, 1.0f}};
        vkCmdClearColorImage(extra[0], image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                             &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(extra[0], VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                             NULL, 1, &barrier);
        assert(vkEndCommandBuffer(extra[0]) == VK_SUCCESS);
        assert(frame_pacer_hud_record(
            &record_provider, draw.command_buffers[0], image,
            draw.framebuffers[0], draw.render_pass, &pipeline, &vertices,
            &texture, &context->commands, extent, geometry.count));
        assert(texture.uploaded);
        assert(vkResetCommandBuffer(extra[1], 0) == VK_SUCCESS);
        assert(vkBeginCommandBuffer(extra[1], &begin) == VK_SUCCESS);
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(
            extra[1], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        const VkBufferImageCopy copy = {
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {width, height, 1}};
        vkCmdCopyImageToBuffer(extra[1], image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               read_buffer, 1, &copy);
        const VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(extra[1], VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL,
                             0, NULL);
        assert(vkEndCommandBuffer(extra[1]) == VK_SUCCESS);
        VkCommandBuffer submitted[] = {extra[0], draw.command_buffers[0],
                                       extra[1]};
        const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                     .commandBufferCount = 3,
                                     .pCommandBuffers = submitted};
        assert(vkResetFences(device, 1, &draw.fences[0]) == VK_SUCCESS);
        assert(vkQueueSubmit(context->queue, 1, &submit, draw.fences[0]) ==
               VK_SUCCESS);
        assert(vkWaitForFences(device, 1, &draw.fences[0], VK_TRUE,
                               UINT64_MAX) == VK_SUCCESS);
        unsigned char *expected = malloc(bytes);
        assert(expected);
        for (size_t i = 0; i < bytes; i += 4) {
            expected[i] = encode(0.25f, srgb);
            expected[i + 1] = encode(0.375f, srgb);
            expected[i + 2] = encode(0.5f, srgb);
            expected[i + 3] = 255;
        }
        const struct frame_pacer_font_atlas *atlas = geometry.atlas;
        const unsigned char *coverage = frame_pacer_font_atlas_pixels(atlas);
        for (unsigned int i = 0; i < geometry.count; i += 6) {
            const struct frame_pacer_hud_vertex *quad = geometry.data + i;
            unsigned int x0 = (unsigned int)quad[0].position[0],
                         y0 = (unsigned int)quad[0].position[1];
            for (unsigned int y = y0; y < (unsigned int)quad[2].position[1];
                 ++y)
                for (unsigned int x = x0; x < (unsigned int)quad[2].position[0];
                     ++x) {
                    float alpha = quad[0].color[3];
                    if (i) {
                        unsigned int u =
                            (unsigned int)(quad[0].uv[0] * atlas->width +
                                           0.5f) +
                            x - x0;
                        unsigned int v =
                            (unsigned int)(quad[0].uv[1] * atlas->height +
                                           0.5f) +
                            y - y0;
                        alpha *= coverage[v * atlas->width + u] / 255.0f;
                    }
                    unsigned char *pixel =
                        expected + ((size_t)y * width + x) * 4;
                    for (unsigned int c = 0; c < 3; ++c)
                        pixel[c] =
                            encode(quad[0].color[c] * alpha +
                                       decode(pixel[c], srgb) * (1.0f - alpha),
                                   srgb);
                }
        }
        size_t mismatches = 0;
        for (size_t i = 0; i < bytes; ++i) {
            size_t channel = i % 4;
            size_t actual_index =
                bgra && channel != 3 ? i - channel + 2 - channel : i;
            int delta = expected[i] - ((unsigned char *)mapped)[actual_index];
            if (delta < -3 || delta > 3)
                ++mismatches;
        }
        fprintf(
            stderr,
            "Vulkan pixels %ux%u rows=%u format=%u cached=%u: %zu mismatches\n",
            extent.width, extent.height, rows, format, repeat, mismatches);
        assert(!mismatches);
        free(expected);
    }
    vkUnmapMemory(device, read_memory);
    vkDestroyBuffer(device, read_buffer, NULL);
    vkFreeMemory(device, read_memory, NULL);
    frame_pacer_hud_destroy_vertex_buffer(&vertices, &vertex_provider, device,
                                          NULL);
    frame_pacer_hud_destroy_pipeline(&pipeline, &pipeline_provider, device,
                                     NULL);
    frame_pacer_hud_destroy_texture(&texture, &context->commands, device);
    frame_pacer_hud_destroy_draw_resources(&draw, &draw_provider, device, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_memory, NULL);
}

int main(void)
{
    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME};
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = instance_extensions};
    VkInstance instance;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);
    uint32_t count = 0;
    assert(vkEnumeratePhysicalDevices(instance, &count, NULL) == VK_SUCCESS &&
           count);
    VkPhysicalDevice *physicals = calloc(count, sizeof(*physicals));
    assert(physicals && vkEnumeratePhysicalDevices(instance, &count,
                                                   physicals) == VK_SUCCESS);
    unsigned long selected = 0;
    const char *selection = getenv("FRAME_PACER_TEST_GPU");
    if (selection) {
        char *end;
        selected = strtoul(selection, &end, 10);
        assert(*selection && !*end && selected < count);
    }
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; ++i) {
        if (i == selected) {
            physical = physicals[i];
            break;
        }
    }
    assert(physical != VK_NULL_HANDLE);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    fprintf(stderr, "Vulkan pixel test device=%s driver=%u index=%lu/%u\n",
            properties.deviceName, properties.driverVersion, selected, count);
    free(physicals);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    struct context context = {.family = UINT32_MAX};
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            context.family = i;
            break;
        }
    free(families);
    assert(context.family != UINT32_MAX);
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context.family,
        .queueCount = 1,
        .pQueuePriorities = &priority};
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions};
    assert(vkCreateDevice(physical, &device_info, NULL, &context.device) ==
           VK_SUCCESS);
    vkGetDeviceQueue(context.device, context.family, 0, &context.queue);
    vkGetPhysicalDeviceMemoryProperties(physical, &context.memory);
    assert(frame_pacer_hud_resolve_commands(
        &context.commands, vkGetDeviceProcAddr, context.device));
    const VkExtent2D extents[] = {{1280, 720},  {1440, 900},  {1920, 1080},
                                  {2560, 1440}, {2560, 1600}, {3840, 2160},
                                  {3440, 1440}, {87, 400},    {100, 100}};
    for (unsigned int i = 0; i < sizeof(extents) / sizeof(extents[0]); ++i)
        for (unsigned int rows = 3; rows <= 4; ++rows)
            check_pixels(&context, extents[i], rows, VK_FORMAT_R8G8B8A8_UNORM);
    check_pixels(&context, (VkExtent2D){1440, 900}, 4,
                 VK_FORMAT_B8G8R8A8_UNORM);
    check_pixels(&context, (VkExtent2D){1440, 900}, 4, VK_FORMAT_R8G8B8A8_SRGB);
    vkDestroyDevice(context.device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
