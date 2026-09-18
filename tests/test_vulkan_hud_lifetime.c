#include "vulkan_layer_hud.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static unsigned int destroyed[4];

static void quiet_log(const char *format, ...)
{
    (void)format;
}

static VkResult VKAPI_CALL images(VkDevice device, VkSwapchainKHR swapchain,
                                  uint32_t *count, VkImage *out)
{
    (void)device;
    if (out)
        out[0] = (VkImage)(uintptr_t)swapchain;
    *count = 1;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL view(VkDevice device,
                                const VkImageViewCreateInfo *info,
                                const VkAllocationCallbacks *allocator,
                                VkImageView *out)
{
    (void)device;
    (void)allocator;
    *out = (VkImageView)(uintptr_t)info->image;
    return VK_SUCCESS;
}

static void VKAPI_CALL destroy_view(VkDevice device, VkImageView image,
                                    const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)allocator;
    unsigned int index = (unsigned int)(uintptr_t)image;
    assert(index > 0 && index < 4);
    assert(++destroyed[index] == 1);
}

static void remove_one(struct frame_pacer_vulkan_hud *hud, unsigned int id)
{
    frame_pacer_vulkan_registry_lock(hud->registry);
    struct frame_pacer_vulkan_hud_swapchain *item =
        frame_pacer_vulkan_hud_take_swapchain_locked(
            hud, (VkSwapchainKHR)(uintptr_t)id);
    frame_pacer_vulkan_registry_unlock(hud->registry);
    frame_pacer_vulkan_hud_destroy_swapchain_list(item);
}

static void exercise(unsigned int first, bool device_cleanup)
{
    struct frame_pacer_vulkan_registry registry =
        FRAME_PACER_VULKAN_REGISTRY_INITIALIZER(quiet_log);
    struct frame_pacer_vulkan_hud hud = {.registry = &registry,
                                         .log = quiet_log};
    struct frame_pacer_vulkan_device devices[2] = {0};
    const VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .imageExtent = {640, 480},
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};

    for (unsigned int i = 0; i < 2; ++i) {
        devices[i].hud.commands_ready = true;
        devices[i].hud.resources = (struct frame_pacer_hud_vulkan_provider){
            images, view, destroy_view};
    }
    for (unsigned int i = 1; i <= 3; ++i) {
        destroyed[i] = 0;
        frame_pacer_vulkan_hud_create_swapchain_resources(
            &hud, &devices[i == 2], (VkSwapchainKHR)(uintptr_t)i, &info);
    }
    remove_one(&hud, 99);
    if (device_cleanup) {
        frame_pacer_vulkan_registry_lock(&registry);
        struct frame_pacer_vulkan_hud_swapchain *items =
            frame_pacer_vulkan_hud_take_device_swapchains_locked(&hud,
                                                                 &devices[0]);
        frame_pacer_vulkan_registry_unlock(&registry);
        frame_pacer_vulkan_hud_destroy_swapchain_list(items);
        assert(destroyed[1] == 1 && destroyed[3] == 1 && destroyed[2] == 0);
        remove_one(&hud, 2);
    } else {
        remove_one(&hud, first);
        for (unsigned int i = 1; i <= 3; ++i)
            assert(destroyed[i] == (i == first));
        remove_one(&hud, first);
        /* Exercise the survivor's real lookup/FPS state before cleanup. */
        for (unsigned int i = 1; i <= 3; ++i) {
            if (i == first)
                continue;
            VkSwapchainKHR handle = (VkSwapchainKHR)(uintptr_t)i;
            const VkPresentInfoKHR present = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .swapchainCount = 1,
                .pSwapchains = &handle};
            frame_pacer_vulkan_registry_lock(&registry);
            frame_pacer_vulkan_hud_note_present(&hud, &present, 1000000000);
            frame_pacer_vulkan_registry_unlock(&registry);
            remove_one(&hud, i);
        }
    }
    assert(hud.swapchains == NULL);
    for (unsigned int i = 1; i <= 3; ++i)
        assert(destroyed[i] == 1);
    remove_one(&hud, 1);
    assert(pthread_mutex_destroy(&registry.lock) == 0);
}

int main(void)
{
    for (unsigned int iteration = 0; iteration < 100; ++iteration) {
        exercise(3, false);
        exercise(2, false);
        exercise(1, false);
        exercise(0, true);
    }
    puts("Vulkan HUD lifetime tests passed");
    return 0;
}
