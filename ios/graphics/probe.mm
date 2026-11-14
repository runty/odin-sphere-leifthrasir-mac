// Diagnostic GPU submission/readback, separate from game rendering.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char *);
extern "C" VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *);

struct GpuProbe {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = nullptr;
    bool in_flight = false;
    PFN_vkGetDeviceProcAddr get_device = nullptr;
    template<class T> T inst(const char *name) {
        auto value = vk_icdGetInstanceProcAddr(instance, name);
        if (!value) throw std::runtime_error(std::string("missing instance function: ") + name);
        return reinterpret_cast<T>(value);
    }
    template<class T> T dev(const char *name) {
        auto value = get_device(device, name);
        if (!value) throw std::runtime_error(std::string("missing device function: ") + name);
        return reinterpret_cast<T>(value);
    }
    ~GpuProbe() noexcept {
        try {
        // On a timed-out GPU submission, keep this one diagnostic allocation
        // alive until process teardown instead of freeing in-flight resources.
        if (in_flight) return;
        if (device && get_device) {
            if (fence) dev<PFN_vkDestroyFence>("vkDestroyFence")(device, fence, nullptr);
            if (pool) dev<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool, nullptr);
            if (mapped) dev<PFN_vkUnmapMemory>("vkUnmapMemory")(device, memory);
            if (buffer) dev<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, buffer, nullptr);
            if (memory) dev<PFN_vkFreeMemory>("vkFreeMemory")(device, memory, nullptr);
            dev<PFN_vkDestroyDevice>("vkDestroyDevice")(device, nullptr);
        }
        if (instance) inst<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
        } catch (const std::exception &error) { NSLog(@"Odin GPU cleanup incomplete: %s", error.what()); }
    }
};
static void checked(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}

extern "C" NSDictionary *osl_mobile_gpu_report() {
    const char *stage = "capabilities";
    @autoreleasepool {
        try {
            id<MTLDevice> metal = MTLCreateSystemDefaultDevice();
            if (!metal || ![metal supportsFamily:MTLGPUFamilyMetal4])
                return @{@"passed": @NO, @"game_frames": @NO, @"error": @"Metal 4 family unavailable"};
            NSString *cache = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"OdinGpuProbe"];
            [NSFileManager.defaultManager createDirectoryAtPath:cache withIntermediateDirectories:YES attributes:nil error:NULL];
            setenv("XDG_CACHE_HOME", cache.fileSystemRepresentation, 1);
            GpuProbe gpu;
            uint32_t interface_version = 7;
            checked(vk_icdNegotiateLoaderICDInterfaceVersion(&interface_version), "ICD negotiation");
            stage = "instance";
            VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            application.pApplicationName = "Odin GPU Probe";
            application.apiVersion = VK_API_VERSION_1_3;
            VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            ci.pApplicationInfo = &application;
            checked(gpu.inst<PFN_vkCreateInstance>("vkCreateInstance")(&ci, nullptr, &gpu.instance), "instance");
            uint32_t count = 0;
            const auto enumerate = gpu.inst<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
            checked(enumerate(gpu.instance, &count, nullptr), "physical device count");
            if (!count || count > 16) throw std::runtime_error("invalid physical device count");
            std::array<VkPhysicalDevice, 16> devices{};
            checked(enumerate(gpu.instance, &count, devices.data()), "physical devices");
            VkPhysicalDevice physical = devices[0];
            VkPhysicalDeviceProperties properties{};
            gpu.inst<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(physical, &properties);
            uint32_t families = 0;
            const auto get_queues = gpu.inst<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
            get_queues(physical, &families, nullptr);
            if (!families || families > 64) throw std::runtime_error("invalid queue count");
            std::array<VkQueueFamilyProperties, 64> queues{};
            get_queues(physical, &families, queues.data());
            uint32_t family = 0;
            while (family < families && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
            if (family == families) throw std::runtime_error("no graphics queue");
            stage = "device";
            const float priority = 1;
            VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
            VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
            checked(gpu.inst<PFN_vkCreateDevice>("vkCreateDevice")(physical, &dci, nullptr, &gpu.device), "device");
            gpu.get_device = gpu.inst<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
            VkQueue queue = VK_NULL_HANDLE;
            gpu.dev<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(gpu.device, family, 0, &queue);
            stage = "allocation";
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size = 256; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            checked(gpu.dev<PFN_vkCreateBuffer>("vkCreateBuffer")(gpu.device, &bci, nullptr, &gpu.buffer), "buffer");
            VkMemoryRequirements requirements{};
            gpu.dev<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(gpu.device, gpu.buffer, &requirements);
            VkPhysicalDeviceMemoryProperties memory_properties{};
            gpu.inst<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical, &memory_properties);
            uint32_t memory_type = UINT32_MAX;
            for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
                const auto flags = memory_properties.memoryTypes[i].propertyFlags;
                if ((requirements.memoryTypeBits & (1u << i)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                    memory_type = i;
                    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
                }
            }
            if (memory_type == UINT32_MAX) throw std::runtime_error("no host-visible memory");
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize = requirements.size; ai.memoryTypeIndex = memory_type;
            checked(gpu.dev<PFN_vkAllocateMemory>("vkAllocateMemory")(gpu.device, &ai, nullptr, &gpu.memory), "memory");
            checked(gpu.dev<PFN_vkBindBufferMemory>("vkBindBufferMemory")(gpu.device, gpu.buffer, gpu.memory, 0), "bind");
            checked(gpu.dev<PFN_vkMapMemory>("vkMapMemory")(gpu.device, gpu.memory, 0, VK_WHOLE_SIZE, 0, &gpu.mapped), "map");
            VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pci.queueFamilyIndex = family;
            checked(gpu.dev<PFN_vkCreateCommandPool>("vkCreateCommandPool")(gpu.device, &pci, nullptr, &gpu.pool), "pool");
            VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cai.commandPool = gpu.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
            VkCommandBuffer command = VK_NULL_HANDLE;
            checked(gpu.dev<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(gpu.device, &cai, &command), "command");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            checked(gpu.dev<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command, &begin), "begin");
            gpu.dev<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(command, gpu.buffer, 0, 256, 0x5a3cc3a5);
            // Make the transfer visible to host reads after fence completion.
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            gpu.dev<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            checked(gpu.dev<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command), "end");
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            checked(gpu.dev<PFN_vkCreateFence>("vkCreateFence")(gpu.device, &fci, nullptr, &gpu.fence), "fence");
            stage = "submission";
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            checked(gpu.dev<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &submit, gpu.fence), "submit");
            gpu.in_flight = true;
            checked(gpu.dev<PFN_vkWaitForFences>("vkWaitForFences")(gpu.device, 1, &gpu.fence, VK_TRUE, 5000000000ull), "wait");
            gpu.in_flight = false;
            stage = "readback";
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = gpu.memory; range.size = VK_WHOLE_SIZE;
            checked(gpu.dev<PFN_vkInvalidateMappedMemoryRanges>("vkInvalidateMappedMemoryRanges")(gpu.device, 1, &range), "invalidate");
            unsigned matched = 0;
            for (unsigned i = 0; i < 64; ++i) {
                uint32_t value = 0; std::memcpy(&value, static_cast<char *>(gpu.mapped) + i * 4, 4);
                matched += value == 0x5a3cc3a5;
            }
            return @{@"passed": @(matched == 64), @"game_frames": @NO, @"matched_words": @(matched),
                     @"expected_words": @64, @"device": @(properties.deviceName), @"icd_interface": @(interface_version),
                     @"test": @"Vulkan fill, queue fence and mapped GPU readback"};
        } catch (const std::exception &error) {
            return @{@"passed": @NO, @"game_frames": @NO, @"stage": @(stage), @"error": @(error.what())};
        }
    }
}
