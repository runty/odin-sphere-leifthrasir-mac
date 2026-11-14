// Exercise the launcher's intended x86-64 Vulkan loader and the renderer's required features.
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const required[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
};

static bool check_device(VkPhysicalDevice gpu) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    printf("%s: Vulkan %u.%u\n", props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *ext = calloc(count, sizeof(*ext));
    if (!ext) return false;
    bool ok = vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, ext) == VK_SUCCESS;
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        bool found = false;
        for (uint32_t j = 0; j < count; ++j) found |= strcmp(required[i], ext[j].extensionName) == 0;
        printf("  %s %s\n", required[i], found ? "yes" : "missing");
        ok &= found;
    }
    free(ext);
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT divisor = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT};
    VkPhysicalDeviceRobustness2FeaturesEXT robust = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &divisor};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &robust};
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    printf("  robustBufferAccess2=%u robustImageAccess2=%u nullDescriptor=%u vertexAttributeInstanceRateDivisor=%u\n",
           robust.robustBufferAccess2, robust.robustImageAccess2, robust.nullDescriptor, divisor.vertexAttributeInstanceRateDivisor);
    if (!ok || props.apiVersion < VK_API_VERSION_1_3 || !robust.robustBufferAccess2 ||
        !robust.robustImageAccess2 || !robust.nullDescriptor || !divisor.vertexAttributeInstanceRateDivisor) return false;
    uint32_t queues = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queues, NULL);
    VkQueueFamilyProperties *families = calloc(queues, sizeof(*families));
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queues, families);
    uint32_t index = 0;
    while (index < queues && !(families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++index;
    free(families);
    if (index == queues) return false;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = index, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &robust,
        .enabledExtensionCount = sizeof(required) / sizeof(required[0]), .ppEnabledExtensionNames = required,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
    VkDevice device;
    VkResult result = vkCreateDevice(gpu, &create, NULL, &device);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateDevice: %d\n", result); return false; }
    VkQueue queue;
    vkGetDeviceQueue(device, index, 0, &queue);
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    result = vkCreateFence(device, &fence_info, NULL, &fence);
    if (result == VK_SUCCESS) {
        result = vkQueueSubmit(queue, 0, NULL, fence);
        if (result == VK_SUCCESS) result = vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
        if (result == VK_SUCCESS) vkDestroyFence(device, fence, NULL);
        else { fprintf(stderr, "graphics queue did not complete: %d\n", result); exit(1); }
    }
    vkDestroyDevice(device, NULL);
    if (result == VK_SUCCESS) puts("  logical device and graphics queue fence passed");
    return result == VK_SUCCESS;
}

int main(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Odin Vulkan probe",
                             .apiVersion = VK_API_VERSION_1_3};
    const char *extensions[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR, .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
    VkInstance instance;
    VkResult result = vkCreateInstance(&info, NULL, &instance);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance: %d\n", result); return 1; }
    uint32_t count = 0;
    bool ok = false;
    if (vkEnumeratePhysicalDevices(instance, &count, NULL) == VK_SUCCESS && count) {
        VkPhysicalDevice *gpus = calloc(count, sizeof(*gpus));
        if (gpus && vkEnumeratePhysicalDevices(instance, &count, gpus) == VK_SUCCESS)
            for (uint32_t i = 0; i < count; ++i) ok |= check_device(gpus[i]);
        free(gpus);
    }
    vkDestroyInstance(instance, NULL);
    return ok ? 0 : 1;
}
