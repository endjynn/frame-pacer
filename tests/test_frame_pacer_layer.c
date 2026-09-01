#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

void frame_pacer_layer_test_fail_next_allocation(void);
void frame_pacer_layer_test_fail_allocation_after(size_t);
uint32_t frame_pacer_layer_test_queue_family_count(VkPhysicalDevice);
uint32_t frame_pacer_layer_test_physical_device_count(void);
uint32_t frame_pacer_layer_test_queue_count(void);

static VkInstance fake_instance = (VkInstance)(uintptr_t)1;
static VkPhysicalDevice fake_physical = (VkPhysicalDevice)(uintptr_t)2;
static VkDevice fake_device = (VkDevice)(uintptr_t)3;
static VkQueue fake_queue = (VkQueue)(uintptr_t)4;
static unsigned int instance_creates;
static unsigned int instance_destroys;
static unsigned int instance_destroy_lookups;
static unsigned int device_creates;
static unsigned int device_destroys;
static const char *missing_gipa;
static const char *missing_gdpa;

static VKAPI_ATTR void VKAPI_CALL dummy_command(void)
{
}

static VKAPI_ATTR VkResult VKAPI_CALL fake_create_instance(
    const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator,
    VkInstance *instance)
{
    (void)info;
    (void)allocator;
    ++instance_creates;
    *instance = fake_instance;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fake_destroy_instance(
    VkInstance instance, const VkAllocationCallbacks *allocator)
{
    (void)allocator;
    assert(instance == fake_instance);
    ++instance_destroys;
}

static VKAPI_ATTR VkResult VKAPI_CALL fake_enumerate_physical_devices(
    VkInstance instance, uint32_t *count, VkPhysicalDevice *devices)
{
    assert(instance == fake_instance);
    if (!devices) {
        *count = 1;
    } else {
        assert(*count == 1);
        devices[0] = fake_physical;
        /* A broken provider must not make the layer consume beyond capacity. */
        *count = 2;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fake_get_queue_families(
    VkPhysicalDevice physical, uint32_t *count,
    VkQueueFamilyProperties *properties)
{
    assert(physical == fake_physical);
    if (!properties) {
        *count = 1;
    } else {
        assert(*count == 1);
        memset(properties, 0, sizeof(*properties));
        properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        properties[0].queueCount = 1;
        *count = 2;
    }
}

static VKAPI_ATTR void VKAPI_CALL fake_get_memory_properties(
    VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties *properties)
{
    assert(physical == fake_physical);
    memset(properties, 0, sizeof(*properties));
}

static VKAPI_ATTR VkResult VKAPI_CALL fake_create_device(
    VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDevice *device)
{
    (void)info;
    (void)allocator;
    assert(physical == fake_physical);
    ++device_creates;
    *device = fake_device;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL fake_destroy_device(
    VkDevice device, const VkAllocationCallbacks *allocator)
{
    (void)allocator;
    assert(device == fake_device);
    ++device_destroys;
}

static VKAPI_ATTR void VKAPI_CALL fake_get_device_queue(
    VkDevice device, uint32_t family, uint32_t index, VkQueue *queue)
{
    assert(device == fake_device);
    assert(family == 0 && index == 0);
    *queue = fake_queue;
}

static VKAPI_ATTR VkResult VKAPI_CALL fake_set_device_loader_data(
    VkDevice device, void *object)
{
    assert(device == fake_device);
    assert(object == fake_queue);
    return VK_SUCCESS;
}

static PFN_vkVoidFunction VKAPI_CALL fake_gdpa(VkDevice device,
                                               const char *name)
{
    assert(device == fake_device);
    if (missing_gdpa && !strcmp(name, missing_gdpa))
        return 0;
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)fake_destroy_device;
    if (!strcmp(name, "vkGetDeviceQueue"))
        return (PFN_vkVoidFunction)fake_get_device_queue;
    return (PFN_vkVoidFunction)dummy_command;
}

static PFN_vkVoidFunction VKAPI_CALL fake_gipa(VkInstance instance,
                                               const char *name)
{
    if (missing_gipa && !strcmp(name, missing_gipa)) return 0;
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction)fake_create_instance;
    if (!strcmp(name, "vkDestroyInstance")) {
        ++instance_destroy_lookups;
        return (PFN_vkVoidFunction)fake_destroy_instance;
    }
    assert(instance == fake_instance);
    if (!strcmp(name, "vkEnumeratePhysicalDevices"))
        return (PFN_vkVoidFunction)fake_enumerate_physical_devices;
    if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties"))
        return (PFN_vkVoidFunction)fake_get_queue_families;
    if (!strcmp(name, "vkGetPhysicalDeviceMemoryProperties"))
        return (PFN_vkVoidFunction)fake_get_memory_properties;
    if (!strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction)fake_create_device;
    if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return dummy_command;
    return (PFN_vkVoidFunction)dummy_command;
}

static VkInstanceCreateInfo instance_info(VkLayerInstanceCreateInfo *loader,
                                          VkLayerInstanceLink *link)
{
    *link = (VkLayerInstanceLink){
        .pfnNextGetInstanceProcAddr = fake_gipa,
    };
    *loader = (VkLayerInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
        .function = VK_LAYER_LINK_INFO,
        .u.pLayerInfo = link,
    };
    return (VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = loader,
    };
}

static VkDeviceCreateInfo device_info(VkLayerDeviceCreateInfo *loader,
                                      VkLayerDeviceCreateInfo *data,
                                      VkLayerDeviceLink *link)
{
    static const float priority = 1.0f;
    static const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    *link = (VkLayerDeviceLink){
        .pfnNextGetInstanceProcAddr = fake_gipa,
        .pfnNextGetDeviceProcAddr = fake_gdpa,
    };
    *data = (VkLayerDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
        .function = VK_LOADER_DATA_CALLBACK,
        .u.pfnSetDeviceLoaderData = fake_set_device_loader_data,
    };
    *loader = (VkLayerDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
        .pNext = data,
        .function = VK_LAYER_LINK_INFO,
        .u.pLayerInfo = link,
    };
    return (VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = loader,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
    };
}

int main(void)
{
    VkLayerInstanceCreateInfo instance_loader;
    VkLayerInstanceLink instance_link;
    VkLayerDeviceCreateInfo device_loader;
    VkLayerDeviceCreateInfo device_data;
    VkLayerDeviceLink device_link;
    VkInstanceCreateInfo create_instance;
    VkDeviceCreateInfo create_device;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    assert(vkCreateInstance(0, 0, &instance) == VK_ERROR_INITIALIZATION_FAILED);
    create_instance = instance_info(&instance_loader, &instance_link);
    assert(vkCreateInstance(&create_instance, 0, 0) ==
           VK_ERROR_INITIALIZATION_FAILED);

    create_instance = instance_info(&instance_loader, &instance_link);
    frame_pacer_layer_test_fail_next_allocation();
    assert(vkCreateInstance(&create_instance, 0, &instance) ==
           VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(instance == VK_NULL_HANDLE);
    assert(instance_creates == 1 && instance_destroys == 1);
    assert(frame_pacer_layer_test_physical_device_count() == 0);

    create_instance = instance_info(&instance_loader, &instance_link);
    assert(vkCreateInstance(&create_instance, 0, &instance) == VK_SUCCESS);
    assert(instance == fake_instance);
    assert(instance_destroy_lookups == 2);
    assert(frame_pacer_layer_test_physical_device_count() == 1);
    assert(frame_pacer_layer_test_queue_family_count(fake_physical) == 1);
    assert(vkGetInstanceProcAddr(instance, "vkFramePacerUnknown") ==
           (PFN_vkVoidFunction)dummy_command);

    create_device = device_info(&device_loader, &device_data, &device_link);
    missing_gipa = "vkCreateDevice";
    assert(vkCreateDevice(fake_physical, &create_device, 0, &device) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(device == VK_NULL_HANDLE && device_creates == 0);
    missing_gipa = 0;

    create_device = device_info(&device_loader, &device_data, &device_link);
    missing_gdpa = "vkGetDeviceQueue";
    assert(vkCreateDevice(fake_physical, &create_device, 0, &device) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(device == VK_NULL_HANDLE);
    assert(device_creates == 1 && device_destroys == 1);
    assert(frame_pacer_layer_test_queue_count() == 0);
    missing_gdpa = 0;

    create_device = device_info(&device_loader, &device_data, &device_link);
    frame_pacer_layer_test_fail_next_allocation();
    assert(vkCreateDevice(fake_physical, &create_device, 0, &device) ==
           VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(device == VK_NULL_HANDLE);
    assert(device_creates == 2 && device_destroys == 2);
    assert(frame_pacer_layer_test_queue_count() == 0);

    create_device = device_info(&device_loader, &device_data, &device_link);
    frame_pacer_layer_test_fail_allocation_after(1);
    assert(vkCreateDevice(fake_physical, &create_device, 0, &device) ==
           VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(device == VK_NULL_HANDLE);
    assert(device_creates == 3 && device_destroys == 3);
    assert(frame_pacer_layer_test_queue_count() == 0);

    create_device = device_info(&device_loader, &device_data, &device_link);
    assert(vkCreateDevice(fake_physical, &create_device, 0, &device) == VK_SUCCESS);
    assert(device == fake_device);
    assert(frame_pacer_layer_test_queue_count() == 1);
    assert(vkGetDeviceProcAddr(device, "vkFramePacerUnknown") ==
           (PFN_vkVoidFunction)dummy_command);
    vkDestroyDevice(device, 0);
    assert(device_destroys == 4);
    assert(frame_pacer_layer_test_queue_count() == 0);
    vkDestroyInstance(instance, 0);
    assert(instance_destroys == 2);
    assert(instance_destroy_lookups == 2);
    assert(frame_pacer_layer_test_physical_device_count() == 0);
    return 0;
}
