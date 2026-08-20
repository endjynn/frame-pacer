#include <vulkan/vulkan.h>
#include <stdio.h>
int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    VkResult result = vkCreateInstance(&info, 0, &instance);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance: %d\n", result); return 1; }
    vkDestroyInstance(instance, 0);
    return 0;
}
