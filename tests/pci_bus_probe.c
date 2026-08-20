#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>

static int has_pci_extension(VkPhysicalDevice device)
{
    VkExtensionProperties extensions[256];
    uint32_t count = 256, index;
    if (vkEnumerateDeviceExtensionProperties(device, 0, &count, extensions) != VK_SUCCESS) return 0;
    for (index = 0; index < count; ++index)
        if (!strcmp(extensions[index].extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)) return 1;
    return 0;
}

int main(void)
{
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    VkPhysicalDevice devices[16];
    uint32_t count = 16, index;
    if (vkCreateInstance(&create, 0, &instance) != VK_SUCCESS) return 1;
    if (vkEnumeratePhysicalDevices(instance, &count, devices) != VK_SUCCESS) { vkDestroyInstance(instance, 0); return 2; }
    for (index = 0; index < count; ++index) {
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDevicePCIBusInfoPropertiesEXT pci = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
        properties.pNext = has_pci_extension(devices[index]) ? &pci : 0;
        vkGetPhysicalDeviceProperties2(devices[index], &properties);
        printf("vendor=0x%04x device=0x%04x name=%s pci=%u:%u:%u.%u\n",
               properties.properties.vendorID, properties.properties.deviceID, properties.properties.deviceName,
               pci.pciDomain, pci.pciBus, pci.pciDevice, pci.pciFunction);
    }
    vkDestroyInstance(instance, 0);
    return 0;
}
