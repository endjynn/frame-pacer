#include "vulkan_layer_hud.h"

#include "hud_spv.h"
#include "hud_vulkan_draw_resources.h"
#include "hud_vulkan_pipeline.h"
#include "hud_vulkan_present.h"
#include "hud_vulkan_record.h"
#include "hud_vulkan_resources.h"
#include "hud_vulkan_vertex_buffer.h"
#include "hud_vertices.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct frame_pacer_vulkan_hud_swapchain {
    VkSwapchainKHR handle;
    struct frame_pacer_vulkan_hud *owner;
    struct frame_pacer_vulkan_device *device;
    struct frame_pacer_hud_image_views image_views;
    VkExtent2D extent;
    VkFormat format;
    struct frame_pacer_hud_draw_resources draw_resources;
    struct frame_pacer_hud_pipeline pipeline;
    struct frame_pacer_hud_vertex_buffer vertex_buffer;
    struct frame_pacer_hud_vertices vertices;
    bool draw_setup_attempted;
    bool disabled;
    bool submitted;
    /* Wine may map an implicit layer into a separate linker namespace, so the
     * tracker remains beside the command buffer that consumes it. */
    struct frame_pacer_fps_tracker fps;
    struct frame_pacer_vulkan_hud_swapchain *next;
};

static struct frame_pacer_vulkan_hud_swapchain *
find_swapchain(struct frame_pacer_vulkan_hud *hud, VkSwapchainKHR swapchain)
{
    struct frame_pacer_vulkan_hud_swapchain *item;

    for (item = hud ? hud->swapchains : 0; item; item = item->next)
        if (item->handle == swapchain)
            return item;
    return 0;
}

static struct frame_pacer_vulkan_hud_swapchain *
take_swapchain(struct frame_pacer_vulkan_hud *hud, VkSwapchainKHR swapchain)
{
    struct frame_pacer_vulkan_hud_swapchain **link = &hud->swapchains;

    while (*link) {
        if ((*link)->handle == swapchain) {
            struct frame_pacer_vulkan_hud_swapchain *found = *link;

            *link = found->next;
            return found;
        }
        link = &(*link)->next;
    }
    return 0;
}

static struct frame_pacer_vulkan_hud_swapchain *
take_device_swapchains(struct frame_pacer_vulkan_hud *hud,
                       struct frame_pacer_vulkan_device *device)
{
    struct frame_pacer_vulkan_hud_swapchain **link = &hud->swapchains;
    struct frame_pacer_vulkan_hud_swapchain *result = 0;

    while (*link) {
        if ((*link)->device == device) {
            struct frame_pacer_vulkan_hud_swapchain *found = *link;

            *link = found->next;
            found->next = result;
            result = found;
        } else {
            link = &(*link)->next;
        }
    }
    return result;
}

static void destroy_swapchain(struct frame_pacer_vulkan_hud_swapchain *item)
{
    if (!item)
        return;
    frame_pacer_hud_destroy_pipeline(
        &item->pipeline, &item->device->hud.pipeline, item->device->handle, 0);
    frame_pacer_hud_destroy_vertex_buffer(&item->vertex_buffer,
                                          &item->device->hud.vertex_buffer,
                                          item->device->handle, 0);
    frame_pacer_hud_destroy_draw_resources(&item->draw_resources,
                                           &item->device->hud.draw,
                                           item->device->handle, 0);
    frame_pacer_hud_destroy_image_views(&item->image_views,
                                        &item->device->hud.resources,
                                        item->device->handle, 0);
    frame_pacer_fps_destroy(&item->fps);
    free(item);
}

static void
destroy_swapchain_list(struct frame_pacer_vulkan_hud_swapchain *items)
{
    while (items) {
        struct frame_pacer_vulkan_hud_swapchain *next = items->next;

        destroy_swapchain(items);
        items = next;
    }
}

void frame_pacer_vulkan_hud_create_swapchain_resources(
    struct frame_pacer_vulkan_hud *hud,
    struct frame_pacer_vulkan_device *device, VkSwapchainKHR swapchain,
    const VkSwapchainCreateInfoKHR *info)
{
    struct frame_pacer_vulkan_hud_swapchain *item;
    enum frame_pacer_hud_resource_status status;

    if (!hud || !device || !swapchain || !info)
        return;
    if (!device->hud.commands_ready) {
        hud->log("frame-pacer: HUD unavailable swapchain=%" PRIx64
                 ": required Vulkan command missing; fail-open\n",
                 (uint64_t)swapchain);
        return;
    }
    item = frame_pacer_vulkan_registry_allocate_zero(1, sizeof(*item));
    if (!item) {
        hud->log("frame-pacer: HUD unavailable swapchain=%" PRIx64
                 ": state allocation failed; fail-open\n",
                 (uint64_t)swapchain);
        return;
    }
    item->handle = swapchain;
    item->owner = hud;
    item->device = device;
    item->extent = info->imageExtent;
    item->format = info->imageFormat;
    frame_pacer_fps_init(&item->fps);
    status = frame_pacer_hud_create_image_views(
        &item->image_views, &device->hud.resources, device->handle, swapchain,
        info->imageFormat, info->imageUsage);
    if (status != FRAME_PACER_HUD_RESOURCE_READY) {
        hud->log("frame-pacer: HUD unavailable swapchain=%" PRIx64
                 ": %s; fail-open\n",
                 (uint64_t)swapchain,
                 frame_pacer_hud_resource_status_string(status));
        destroy_swapchain(item);
        return;
    }
    frame_pacer_vulkan_registry_lock(hud->registry);
    item->next = hud->swapchains;
    hud->swapchains = item;
    frame_pacer_vulkan_registry_unlock(hud->registry);
    hud->log("frame-pacer: HUD image resources ready swapchain=%" PRIx64
             " images=%u\n",
             (uint64_t)swapchain, item->image_views.count);
}

void frame_pacer_vulkan_hud_create_draw_resources(
    struct frame_pacer_vulkan_hud *hud, VkQueue queue,
    const VkPresentInfoKHR *info)
{
    struct frame_pacer_vulkan_queue *queue_state;
    struct frame_pacer_vulkan_hud_swapchain *item;

    if (!hud || !info || info->swapchainCount != 1 || !info->pSwapchains)
        return;
    frame_pacer_vulkan_registry_lock(hud->registry);
    queue_state = frame_pacer_vulkan_registry_find_queue(hud->registry, queue);
    item = take_swapchain(hud, info->pSwapchains[0]);
    if (item) {
        item->next = hud->swapchains;
        hud->swapchains = item;
    }
    if (!item || item->draw_resources.ready || item->draw_setup_attempted ||
        !queue_state || queue_state->device != item->device ||
        !(queue_state->flags & VK_QUEUE_GRAPHICS_BIT)) {
        frame_pacer_vulkan_registry_unlock(hud->registry);
        return;
    }
    item->draw_setup_attempted = true;
    if (!frame_pacer_hud_create_draw_resources(
            &item->draw_resources, &item->device->hud.draw,
            item->device->handle, &item->image_views, item->format,
            item->extent, queue_state->family)) {
        hud->log(
            "frame-pacer: HUD draw resources unavailable swapchain=%" PRIx64
            "; fail-open\n",
            (uint64_t)item->handle);
    } else if (!frame_pacer_hud_create_pipeline(
                   &item->pipeline, &item->device->hud.pipeline,
                   item->device->handle, item->draw_resources.render_pass,
                   (const uint32_t *)build_shaders_hud_vert_spv,
                   sizeof(build_shaders_hud_vert_spv),
                   (const uint32_t *)build_shaders_hud_frag_spv,
                   sizeof(build_shaders_hud_frag_spv))) {
        hud->log("frame-pacer: HUD pipeline unavailable swapchain=%" PRIx64
                 "; fail-open\n",
                 (uint64_t)item->handle);
    } else if (!item->device->hud.has_memory_properties ||
               !frame_pacer_hud_create_vertex_buffer(
                   &item->vertex_buffer, &item->device->hud.vertex_buffer,
                   item->device->handle, &item->device->hud.memory_properties,
                   sizeof(struct frame_pacer_hud_vertices))) {
        hud->log("frame-pacer: HUD vertex buffer unavailable swapchain=%" PRIx64
                 "; fail-open\n",
                 (uint64_t)item->handle);
    } else {
        hud->log("frame-pacer: HUD command resources ready swapchain=%" PRIx64
                 " images=%u family=%u vertex-bytes=%zu\n",
                 (uint64_t)item->handle, item->draw_resources.count,
                 queue_state->family, sizeof(struct frame_pacer_hud_vertices));
    }
    frame_pacer_vulkan_registry_unlock(hud->registry);
}

static bool record_overlay(void *context, uint32_t image_index)
{
    struct frame_pacer_vulkan_hud_swapchain *item = context;
    struct frame_pacer_vulkan_hud *hud;
    struct frame_pacer_hud_text text;
    uint64_t now;

    if (!item || image_index >= item->draw_resources.count ||
        !item->vertex_buffer.map)
        return false;
    hud = item->owner;
    if (!hud || !hud->now || !hud->format_text)
        return false;
    now = hud->now(hud->callback_context);
    hud->format_text(hud->callback_context, item->device, &item->fps, now,
                     &text);
    if (!frame_pacer_hud_vertices_build_for_extent(
            &item->vertices, &text, item->extent.width, item->extent.height) ||
        sizeof(item->vertices.data[0]) * (size_t)item->vertices.count >
            item->vertex_buffer.size)
        return false;
    memcpy(item->vertex_buffer.map, item->vertices.data,
           sizeof(item->vertices.data[0]) * (size_t)item->vertices.count);
    return frame_pacer_hud_record(
        &item->device->hud.record,
        item->draw_resources.command_buffers[image_index],
        item->image_views.images[image_index],
        item->draw_resources.framebuffers[image_index],
        item->draw_resources.render_pass, &item->pipeline, &item->vertex_buffer,
        item->extent, item->vertices.count);
}

const VkPresentInfoKHR *frame_pacer_vulkan_hud_prepare_present(
    struct frame_pacer_vulkan_hud *hud, VkQueue queue,
    const VkPresentInfoKHR *info, VkPresentInfoKHR *replacement)
{
    struct frame_pacer_vulkan_queue *queue_state;
    struct frame_pacer_vulkan_hud_swapchain *item;
    uint32_t image_index;

    if (!hud || !info || !replacement || info->swapchainCount != 1 ||
        !info->pSwapchains || !info->pImageIndices)
        return info;
    frame_pacer_vulkan_registry_lock(hud->registry);
    queue_state = frame_pacer_vulkan_registry_find_queue(hud->registry, queue);
    item = take_swapchain(hud, info->pSwapchains[0]);
    if (item) {
        item->next = hud->swapchains;
        hud->swapchains = item;
    }
    frame_pacer_vulkan_registry_unlock(hud->registry);
    if (!item || item->disabled || !queue_state ||
        queue_state->device != item->device ||
        !(queue_state->flags & VK_QUEUE_GRAPHICS_BIT) ||
        !item->draw_resources.ready || !item->pipeline.pipeline ||
        !item->vertex_buffer.buffer ||
        info->pImageIndices[0] >= item->draw_resources.count)
        return info;
    image_index = info->pImageIndices[0];
    if (!frame_pacer_hud_prepare_present(
            &item->device->hud.present, item->device->handle, queue, info,
            item->draw_resources.fences[image_index],
            &item->draw_resources.semaphores[image_index],
            item->draw_resources.command_buffers[image_index], image_index,
            item->draw_resources.count, record_overlay, item, replacement)) {
        item->disabled = true;
        hud->log(
            "frame-pacer: HUD overlay submission unavailable swapchain=%" PRIx64
            "; fail-open\n",
            (uint64_t)item->handle);
        return info;
    }
    if (!item->submitted) {
        item->submitted = true;
        hud->log("frame-pacer: HUD overlay submitted swapchain=%" PRIx64
                 " image=%u\n",
                 (uint64_t)item->handle, image_index);
    }
    return replacement;
}

void frame_pacer_vulkan_hud_note_present(struct frame_pacer_vulkan_hud *hud,
                                         const VkPresentInfoKHR *info,
                                         uint64_t accepted_ns)
{
    uint32_t index;

    if (!hud || !info || !info->pSwapchains)
        return;
    for (index = 0; index < info->swapchainCount; ++index) {
        struct frame_pacer_vulkan_hud_swapchain *item =
            find_swapchain(hud, info->pSwapchains[index]);

        if (item)
            (void)frame_pacer_fps_record_present(&item->fps, accepted_ns, 0);
    }
}

struct frame_pacer_vulkan_hud_swapchain *
frame_pacer_vulkan_hud_take_swapchain_locked(struct frame_pacer_vulkan_hud *hud,
                                             VkSwapchainKHR swapchain)
{
    return take_swapchain(hud, swapchain);
}

struct frame_pacer_vulkan_hud_swapchain *
frame_pacer_vulkan_hud_take_device_swapchains_locked(
    struct frame_pacer_vulkan_hud *hud,
    struct frame_pacer_vulkan_device *device)
{
    return take_device_swapchains(hud, device);
}

void frame_pacer_vulkan_hud_destroy_swapchain_list(
    struct frame_pacer_vulkan_hud_swapchain *items)
{
    destroy_swapchain_list(items);
}
