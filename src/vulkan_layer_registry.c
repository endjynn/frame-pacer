#include "vulkan_layer_registry.h"

#include <stdlib.h>

#ifdef FRAME_PACER_TEST
static size_t allocations_before_failure = SIZE_MAX;

__attribute__((visibility("hidden")))
void frame_pacer_layer_test_fail_next_allocation(void)
{
    allocations_before_failure = 0;
}

__attribute__((visibility("hidden")))
void frame_pacer_layer_test_fail_allocation_after(size_t successes)
{
    allocations_before_failure = successes;
}
#endif

void *frame_pacer_vulkan_registry_allocate_zero(size_t count, size_t size)
{
#ifdef FRAME_PACER_TEST
    if (allocations_before_failure != SIZE_MAX) {
        if (!allocations_before_failure) {
            allocations_before_failure = SIZE_MAX;
            return 0;
        }
        --allocations_before_failure;
    }
#endif
    return calloc(count, size);
}

void frame_pacer_vulkan_registry_lock(
    struct frame_pacer_vulkan_registry *registry)
{
    if (registry)
        (void)pthread_mutex_lock(&registry->lock);
}

void frame_pacer_vulkan_registry_unlock(
    struct frame_pacer_vulkan_registry *registry)
{
    if (registry)
        (void)pthread_mutex_unlock(&registry->lock);
}

struct frame_pacer_vulkan_instance *frame_pacer_vulkan_registry_find_instance(
    struct frame_pacer_vulkan_registry *registry, VkInstance instance)
{
    struct frame_pacer_vulkan_instance *item;

    for (item = registry ? registry->instances : 0; item; item = item->next)
        if (item->handle == instance)
            return item;
    return 0;
}

struct frame_pacer_vulkan_physical_device *
frame_pacer_vulkan_registry_find_physical_device(
    struct frame_pacer_vulkan_registry *registry, VkPhysicalDevice physical)
{
    struct frame_pacer_vulkan_physical_device *item;

    for (item = registry ? registry->physical_devices : 0; item;
         item = item->next)
        if (item->handle == physical)
            return item;
    return 0;
}

struct frame_pacer_vulkan_device *frame_pacer_vulkan_registry_find_device(
    struct frame_pacer_vulkan_registry *registry, VkDevice device)
{
    struct frame_pacer_vulkan_device *item;

    for (item = registry ? registry->devices : 0; item; item = item->next)
        if (item->handle == device)
            return item;
    return 0;
}

struct frame_pacer_vulkan_queue *frame_pacer_vulkan_registry_find_queue(
    struct frame_pacer_vulkan_registry *registry, VkQueue queue)
{
    struct frame_pacer_vulkan_queue *item;

    for (item = registry ? registry->queues : 0; item; item = item->next)
        if (item->handle == queue)
            return item;
    return 0;
}

static void remember_physical_devices(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_instance *instance)
{
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queues;
    VkPhysicalDevice *handles;
    uint32_t count = 0;
    uint32_t index;

    enumerate = (PFN_vkEnumeratePhysicalDevices)instance->gipa(
        instance->handle, "vkEnumeratePhysicalDevices");
    if (!enumerate || enumerate(instance->handle, &count, 0) != VK_SUCCESS ||
        !count)
        return;

    {
        uint32_t capacity = count;

        handles = frame_pacer_vulkan_registry_allocate_zero(
            capacity, sizeof(*handles));
        if (!handles)
            return;
        get_queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)instance->gipa(
            instance->handle, "vkGetPhysicalDeviceQueueFamilyProperties");
        if (enumerate(instance->handle, &count, handles) != VK_SUCCESS) {
            free(handles);
            return;
        }
        if (count > capacity)
            count = capacity;
    }

    for (index = 0; index < count; ++index) {
        struct frame_pacer_vulkan_physical_device *item =
            frame_pacer_vulkan_registry_allocate_zero(1, sizeof(*item));

        if (item) {
            item->handle = handles[index];
            item->instance = instance;
            if (get_queues) {
                get_queues(handles[index], &item->queue_family_count, 0);
                if (item->queue_family_count) {
                    uint32_t capacity = item->queue_family_count;
                    uint32_t returned = capacity;

                    item->queue_families =
                        frame_pacer_vulkan_registry_allocate_zero(
                            capacity, sizeof(*item->queue_families));
                    if (item->queue_families) {
                        get_queues(handles[index], &returned,
                                   item->queue_families);
                        item->queue_family_count =
                            returned < capacity ? returned : capacity;
                    } else {
                        item->queue_family_count = 0;
                    }
                }
            }
            item->next = registry->physical_devices;
            registry->physical_devices = item;
        }
    }
    free(handles);
}

void frame_pacer_vulkan_registry_add_instance(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_instance *instance)
{
    if (!registry || !instance)
        return;
    frame_pacer_vulkan_registry_lock(registry);
    registry->fallback_gipa = instance->gipa;
    instance->next = registry->instances;
    registry->instances = instance;
    remember_physical_devices(registry, instance);
    frame_pacer_vulkan_registry_unlock(registry);
}

PFN_vkDestroyInstance frame_pacer_vulkan_registry_remove_instance(
    struct frame_pacer_vulkan_registry *registry, VkInstance instance,
    struct frame_pacer_vulkan_instance **removed)
{
    struct frame_pacer_vulkan_instance **instance_link;
    struct frame_pacer_vulkan_physical_device **physical_link;
    struct frame_pacer_vulkan_instance *item;
    PFN_vkDestroyInstance destroy;

    if (removed)
        *removed = 0;
    frame_pacer_vulkan_registry_lock(registry);
    item = frame_pacer_vulkan_registry_find_instance(registry, instance);
    destroy = item && item->gipa
                  ? (PFN_vkDestroyInstance)item->gipa(instance,
                                                       "vkDestroyInstance")
                  : registry->fallback_gipa
                        ? (PFN_vkDestroyInstance)registry->fallback_gipa(
                              instance, "vkDestroyInstance")
                        : 0;
    physical_link = &registry->physical_devices;
    while (*physical_link) {
        if ((*physical_link)->instance == item) {
            struct frame_pacer_vulkan_physical_device *found = *physical_link;

            *physical_link = found->next;
            free(found->queue_families);
            free(found);
        } else {
            physical_link = &(*physical_link)->next;
        }
    }
    instance_link = &registry->instances;
    while (*instance_link && *instance_link != item)
        instance_link = &(*instance_link)->next;
    if (*instance_link)
        *instance_link = item->next;
    registry->fallback_gipa = registry->instances ? registry->instances->gipa : 0;
    frame_pacer_vulkan_registry_unlock(registry);
    if (removed)
        *removed = item;
    return destroy;
}

static void free_queue_list(struct frame_pacer_vulkan_queue *items)
{
    while (items) {
        struct frame_pacer_vulkan_queue *next = items->next;

        free(items);
        items = next;
    }
}

enum frame_pacer_vulkan_queue_collection_result
frame_pacer_vulkan_registry_collect_queues(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_device *device,
    const struct frame_pacer_vulkan_physical_device *physical,
    const VkDeviceCreateInfo *info, struct frame_pacer_vulkan_queue **result)
{
    struct frame_pacer_vulkan_queue *collected = 0;
    uint32_t create_index;
    PFN_vkGetDeviceQueue get_queue;

    if (!result)
        return FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE;
    *result = 0;
    if (!device || !info ||
        (info->queueCreateInfoCount && !info->pQueueCreateInfos))
        return FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE;
    if (!info->queueCreateInfoCount)
        return FRAME_PACER_VULKAN_QUEUES_READY;
    if (!device->gdpa || !device->set_loader_data)
        return FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE;
    get_queue = (PFN_vkGetDeviceQueue)device->gdpa(device->handle,
                                                   "vkGetDeviceQueue");
    if (!get_queue)
        return FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE;

    for (create_index = 0; create_index < info->queueCreateInfoCount;
         ++create_index) {
        const VkDeviceQueueCreateInfo *create =
            &info->pQueueCreateInfos[create_index];
        uint32_t queue_index;

        for (queue_index = 0; queue_index < create->queueCount; ++queue_index) {
            VkQueue queue = VK_NULL_HANDLE;
            struct frame_pacer_vulkan_queue *item;

            get_queue(device->handle, create->queueFamilyIndex, queue_index,
                      &queue);
            if (!queue ||
                device->set_loader_data(device->handle, queue) != VK_SUCCESS) {
                if (registry->log)
                    registry->log("frame-pacer: queue loader-data registration "
                                  "failed family=%u index=%u\n",
                                  create->queueFamilyIndex, queue_index);
                free_queue_list(collected);
                return FRAME_PACER_VULKAN_QUEUES_UNAVAILABLE;
            }
            for (item = collected; item; item = item->next)
                if (item->handle == queue)
                    break;
            if (item)
                continue;
            item = frame_pacer_vulkan_registry_allocate_zero(1, sizeof(*item));
            if (!item) {
                free_queue_list(collected);
                return FRAME_PACER_VULKAN_QUEUES_OUT_OF_MEMORY;
            }
            item->handle = queue;
            item->device = device;
            item->family = create->queueFamilyIndex;
            item->flags =
                physical &&
                        create->queueFamilyIndex < physical->queue_family_count
                    ? physical->queue_families[create->queueFamilyIndex]
                          .queueFlags
                    : 0;
            item->next = collected;
            collected = item;
        }
    }
    *result = collected;
    return FRAME_PACER_VULKAN_QUEUES_READY;
}

void frame_pacer_vulkan_registry_add_device(
    struct frame_pacer_vulkan_registry *registry,
    struct frame_pacer_vulkan_device *device,
    struct frame_pacer_vulkan_queue *queues)
{
    frame_pacer_vulkan_registry_lock(registry);
    device->next = registry->devices;
    registry->devices = device;
    if (queues) {
        struct frame_pacer_vulkan_queue *tail = queues;

        while (tail->next)
            tail = tail->next;
        tail->next = registry->queues;
        registry->queues = queues;
    }
    frame_pacer_vulkan_registry_unlock(registry);
}

struct frame_pacer_vulkan_device *
frame_pacer_vulkan_registry_remove_device_locked(
    struct frame_pacer_vulkan_registry *registry, VkDevice device)
{
    struct frame_pacer_vulkan_device **device_link;
    struct frame_pacer_vulkan_queue **queue_link;
    struct frame_pacer_vulkan_device *item;

    item = frame_pacer_vulkan_registry_find_device(registry, device);
    device_link = &registry->devices;
    while (*device_link && *device_link != item)
        device_link = &(*device_link)->next;
    if (*device_link)
        *device_link = item->next;
    queue_link = &registry->queues;
    while (*queue_link) {
        if ((*queue_link)->device == item) {
            struct frame_pacer_vulkan_queue *found = *queue_link;

            *queue_link = found->next;
            free(found);
        } else {
            queue_link = &(*queue_link)->next;
        }
    }
    return item;
}

#ifdef FRAME_PACER_TEST
static struct frame_pacer_vulkan_registry *test_registry;

void frame_pacer_vulkan_registry_set_test_registry(
    struct frame_pacer_vulkan_registry *registry)
{
    test_registry = registry;
}

__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_queue_family_count(VkPhysicalDevice physical)
{
    struct frame_pacer_vulkan_physical_device *item;
    uint32_t count;

    frame_pacer_vulkan_registry_lock(test_registry);
    item = frame_pacer_vulkan_registry_find_physical_device(test_registry,
                                                             physical);
    count = item ? item->queue_family_count : 0;
    frame_pacer_vulkan_registry_unlock(test_registry);
    return count;
}

__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_physical_device_count(void)
{
    struct frame_pacer_vulkan_physical_device *item;
    uint32_t count = 0;

    frame_pacer_vulkan_registry_lock(test_registry);
    for (item = test_registry->physical_devices; item; item = item->next)
        ++count;
    frame_pacer_vulkan_registry_unlock(test_registry);
    return count;
}

__attribute__((visibility("hidden")))
uint32_t frame_pacer_layer_test_queue_count(void)
{
    struct frame_pacer_vulkan_queue *item;
    uint32_t count = 0;

    frame_pacer_vulkan_registry_lock(test_registry);
    for (item = test_registry->queues; item; item = item->next)
        ++count;
    frame_pacer_vulkan_registry_unlock(test_registry);
    return count;
}
#endif
