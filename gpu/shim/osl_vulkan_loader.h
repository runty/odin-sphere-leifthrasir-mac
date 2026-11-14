#pragma once
#ifdef __APPLE__
#include <dlfcn.h>
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
inline const char *OslVulkanLibraryPath() {
    Dl_info info{};
    return dladdr(reinterpret_cast<const void*>(&vkGetInstanceProcAddr),&info) ? info.dli_fname : "libvulkan.1.dylib";
}
#endif
