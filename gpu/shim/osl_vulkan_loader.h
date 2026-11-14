#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance,const char*);
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t*);
#else
#include <dlfcn.h>
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
inline const char *OslVulkanLibraryPath() {
    Dl_info info{};
    return dladdr(reinterpret_cast<const void*>(&vkGetInstanceProcAddr),&info) ? info.dli_fname : "libvulkan.1.dylib";
}
#endif
#endif
