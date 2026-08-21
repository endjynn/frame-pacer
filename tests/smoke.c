#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned int log_descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    struct dirent *entry;
    unsigned int count = 0;

    if (!directory) return 0;
    while ((entry = readdir(directory))) {
        char path[64];
        char target[4096];
        ssize_t length;

        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name) >=
            (int)sizeof(path))
            continue;
        length = readlink(path, target, sizeof(target) - 1);
        if (length < 0) continue;
        target[length] = '\0';
        if (strstr(target, "/frame-pacer/frame-pacer-")) ++count;
    }
    (void)closedir(directory);
    return count;
}

int main(void)
{
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    unsigned int iteration;

    /* Recreate the instance so layer teardown cannot leave stale dispatch or
     * physical-device metadata hidden by process exit. */
    for (iteration = 0; iteration < 2; ++iteration) {
        VkInstance instance;
        VkResult result = vkCreateInstance(&create_info, 0, &instance);

        if (result != VK_SUCCESS) {
            fprintf(stderr, "vkCreateInstance: %d\n", result);
            return 1;
        }
        vkDestroyInstance(instance, 0);
        {
            unsigned int log_descriptors = log_descriptor_count();

            if (log_descriptors == 0) continue;
            fprintf(stderr,
                    "layer retained %u log descriptors after instance teardown\n",
                    log_descriptors);
            return 2;
        }
    }
    return 0;
}
