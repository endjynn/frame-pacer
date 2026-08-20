#include <vulkan/vulkan.h>
#include <stdio.h>
int main(void) {
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
 VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app}; VkInstance instance; VkResult r=vkCreateInstance(&ci,0,&instance); if(r){fprintf(stderr,"instance %d\n",r);return 1;}
 uint32_t count=0; r=vkEnumeratePhysicalDevices(instance,&count,0); if(r||!count){fprintf(stderr,"physical %d count %u\n",r,count);return 2;} VkPhysicalDevice physical; r=vkEnumeratePhysicalDevices(instance,&count,&physical); if(r){fprintf(stderr,"enumerate %d\n",r);return 3;}
 float priority=1.0f; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority}; VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi}; VkDevice device; r=vkCreateDevice(physical,&di,0,&device); if(r){fprintf(stderr,"device %d\n",r);return 4;} VkQueue queue; vkGetDeviceQueue(device,0,0,&queue); vkDestroyDevice(device,0); vkDestroyInstance(instance,0); return 0;
}
