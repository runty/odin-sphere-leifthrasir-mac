// Native UIKit/SDL and optional Vulkan surface check. No guest execution/presentation.
#define SDL_MAIN_HANDLED 1
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_metal.h>
#include <cstring>
#include <stdexcept>
#if OSL_GPU_PROBE
#define VK_USE_PLATFORM_METAL_EXT 1
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char *);
extern "C" VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *);

static void surfaceChecked(VkResult result,const char *operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": VkResult "+std::to_string(result));
}
struct VulkanSurfaceScope {
    VkInstance instance=VK_NULL_HANDLE;
    VkSurfaceKHR surface=VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroyInstance=nullptr;
    PFN_vkDestroySurfaceKHR destroySurface=nullptr;
    template<class T> T proc(const char *name) {
        auto value=vk_icdGetInstanceProcAddr(instance,name);
        if(!value)throw std::runtime_error(std::string("Missing Vulkan surface function: ")+name);
        return reinterpret_cast<T>(value);
    }
    ~VulkanSurfaceScope() {
        if(surface && destroySurface)destroySurface(instance,surface,nullptr);
        if(instance && destroyInstance)destroyInstance(instance,nullptr);
    }
};
static NSDictionary *checkVulkanSurface(CAMetalLayer *layer) {
    NSMutableDictionary *report=[@{@"passed":@NO,@"vulkan_surface_created":@NO,@"presentation_tested":@NO,@"game_frames":@NO} mutableCopy];
    {
        VulkanSurfaceScope gpu;
        uint32_t version=7;
        surfaceChecked(vk_icdNegotiateLoaderICDInterfaceVersion(&version),"ICD negotiation");
        auto enumerateExtensions=gpu.proc<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
        uint32_t count=0;
        surfaceChecked(enumerateExtensions(nullptr,&count,nullptr),"instance extension count");
        if(!count || count>4096)throw std::runtime_error("Invalid instance extension count");
        std::vector<VkExtensionProperties> extensions(count);
        surfaceChecked(enumerateExtensions(nullptr,&count,extensions.data()),"instance extensions");
        const char *required[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_EXT_METAL_SURFACE_EXTENSION_NAME};
        for(const char *name:required) {
            bool found=false;for(const auto &extension:extensions)found|=!std::strcmp(extension.extensionName,name);
            if(!found)throw std::runtime_error(std::string("Missing required surface extension: ")+name);
        }
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName="Odin UIKit Surface Check";application.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo=&application;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=required;
        surfaceChecked(gpu.proc<PFN_vkCreateInstance>("vkCreateInstance")(&ci,nullptr,&gpu.instance),"surface instance");
        // Instance functions are available only after instance creation.
        gpu.destroyInstance=gpu.proc<PFN_vkDestroyInstance>("vkDestroyInstance");
        gpu.destroySurface=gpu.proc<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
        VkMetalSurfaceCreateInfoEXT surfaceInfo{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
        surfaceInfo.pLayer=layer;
        surfaceChecked(gpu.proc<PFN_vkCreateMetalSurfaceEXT>("vkCreateMetalSurfaceEXT")(gpu.instance,&surfaceInfo,nullptr,&gpu.surface),"Metal surface");
        report[@"vulkan_surface_created"]=@YES;
        auto enumerate=gpu.proc<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        count=0;surfaceChecked(enumerate(gpu.instance,&count,nullptr),"physical device count");
        if(!count || count>16)throw std::runtime_error("Invalid physical device count");
        std::vector<VkPhysicalDevice> devices(count);
        surfaceChecked(enumerate(gpu.instance,&count,devices.data()),"physical devices");
        auto physical=devices[0];
        VkSurfaceCapabilitiesKHR caps{};
        surfaceChecked(gpu.proc<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(physical,gpu.surface,&caps),"surface capabilities");
        count=0;auto formats=gpu.proc<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
        surfaceChecked(formats(physical,gpu.surface,&count,nullptr),"surface format count");
        if(!count || count>4096)throw std::runtime_error("Invalid surface format count");
        std::vector<VkSurfaceFormatKHR> availableFormats(count);
        surfaceChecked(formats(physical,gpu.surface,&count,availableFormats.data()),"surface formats");
        report[@"surface_format_count"]=@(count);
        auto modes=gpu.proc<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
        count=0;surfaceChecked(modes(physical,gpu.surface,&count,nullptr),"present mode count");
        if(!count || count>64)throw std::runtime_error("Invalid present mode count");
        std::vector<VkPresentModeKHR> availableModes(count);
        surfaceChecked(modes(physical,gpu.surface,&count,availableModes.data()),"present modes");
        bool fifo=false;for(auto mode:availableModes)fifo|=mode==VK_PRESENT_MODE_FIFO_KHR;
        if(!fifo)throw std::runtime_error("Required iOS FIFO present mode unavailable");
        auto queues=gpu.proc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        count=0;queues(physical,&count,nullptr);
        if(!count || count>64)throw std::runtime_error("Invalid surface queue count");
        std::vector<VkQueueFamilyProperties> families(count);queues(physical,&count,families.data());
        bool supported=false;uint32_t selected=UINT32_MAX;
        for(uint32_t i=0;i<count;++i) {
            VkBool32 present=VK_FALSE;
            surfaceChecked(gpu.proc<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR")(physical,i,gpu.surface,&present),"queue surface support");
            if(families[i].queueCount && (families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) && present){supported=true;selected=i;break;}
        }
        if(!supported)throw std::runtime_error("No graphics/presentation queue supports this UIKit surface");
        report[@"graphics_present_queue"]=@(selected);report[@"fifo_present_mode"]=@YES;
        report[@"minimum_images"]=@(caps.minImageCount);report[@"maximum_images"]=@(caps.maxImageCount);
        report[@"surface_extent_width"]=@(caps.currentExtent.width);report[@"surface_extent_height"]=@(caps.currentExtent.height);
        report[@"passed"]=@YES;
    }
    report[@"vulkan_surface_destroyed"]=@YES;
    return report;
}
#endif

struct SurfaceScope {
    SDL_PropertiesID properties=0;
    SDL_Window *window=nullptr;
    SDL_MetalView view=nullptr;
    bool video=false;
    ~SurfaceScope() {
        if(view)SDL_Metal_DestroyView(view);
        if(window)SDL_DestroyWindow(window);
        if(properties)SDL_DestroyProperties(properties);
        if(video)SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
extern "C" NSDictionary *osl_mobile_surface_report(UIWindowScene *scene) {
    if(!NSThread.isMainThread)return @{@"passed":@NO,@"error":@"UIKit surface requires the main thread",@"game_frames":@NO};
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)
        return @{@"passed":@NO,@"status":@"waiting_foreground",@"game_frames":@NO};
    if(SDL_WasInit(SDL_INIT_VIDEO))return @{@"passed":@NO,@"error":@"SDL video already owned by another session",@"game_frames":@NO};
    UIWindow *original=scene.keyWindow;
    NSMutableDictionary *report=[@{@"passed":@NO,@"main_thread":@YES,@"scene_supplied":@(scene!=nil),@"vulkan_surface_created":@NO,@"game_executed":@NO,@"game_frames":@NO} mutableCopy];
    const char *stage="SDL UIKit initialization";
    {
        SurfaceScope scope;
        @try {
            try {
                SDL_SetMainReady();
                if(!SDL_InitSubSystem(SDL_INIT_VIDEO))throw std::runtime_error("SDL initialization failed");
                scope.video=true;
                const char *driver=SDL_GetCurrentVideoDriver();
                report[@"video_driver"]=@(driver?driver:"none");
                if(!driver || std::strcmp(driver,"uikit"))throw std::runtime_error("Expected SDL UIKit backend");
                stage="hidden SDL window";
                scope.properties=SDL_CreateProperties();
                if(!scope.properties)throw std::runtime_error("SDL properties unavailable");
                SDL_SetStringProperty(scope.properties,SDL_PROP_WINDOW_CREATE_TITLE_STRING,"Odin Surface Check");
                SDL_SetNumberProperty(scope.properties,SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,960);
                SDL_SetNumberProperty(scope.properties,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,540);
                SDL_SetBooleanProperty(scope.properties,SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,true);
                SDL_SetBooleanProperty(scope.properties,SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN,true);
                SDL_SetBooleanProperty(scope.properties,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,true);
                if(scene)SDL_SetPointerProperty(scope.properties,SDL_PROP_WINDOW_CREATE_WINDOWSCENE_POINTER,(__bridge void *)scene);
                scope.window=SDL_CreateWindowWithProperties(scope.properties);
                if(!scope.window)throw std::runtime_error("SDL window creation failed");
                stage="SDL Metal view";
                scope.view=SDL_Metal_CreateView(scope.window);
                if(!scope.view)throw std::runtime_error("SDL Metal view creation failed");
                CAMetalLayer *layer=(__bridge CAMetalLayer *)SDL_Metal_GetLayer(scope.view);
                if(!layer || ![layer isKindOfClass:CAMetalLayer.class])throw std::runtime_error("SDL did not expose a Metal layer");
                const CGSize size=layer.drawableSize;
                int width=0,height=0;SDL_GetWindowSizeInPixels(scope.window,&width,&height);
                report[@"drawable_width"]=@(size.width);report[@"drawable_height"]=@(size.height);
                report[@"window_pixel_width"]=@(width);report[@"window_pixel_height"]=@(height);
                if(size.width<=0 || size.height<=0 || width<=0 || height<=0)throw std::runtime_error("Metal view has no drawable extent");
                #if OSL_GPU_PROBE
                stage="Vulkan Metal surface and capabilities";
                NSDictionary *vulkan=checkVulkanSurface(layer);
                report[@"vulkan"]=vulkan;
                report[@"vulkan_surface_created"]=vulkan[@"vulkan_surface_created"];
                report[@"passed"]=vulkan[@"passed"];
#else
                report[@"passed"]=@YES;
#endif
            } catch(const std::exception &error) {
                report[@"error"]=@(error.what());report[@"stage"]=@(stage);
                report[@"sdl_error"]=@(SDL_GetError());
            }
        } @catch(NSException *exception) {
            report[@"error"]=exception.name;report[@"stage"]=@(stage);
        }
    }
    const bool clean=!SDL_WasInit(SDL_INIT_VIDEO);
    const bool preserved=!scene || scene.keyWindow==original;
    report[@"cleanup_complete"]=@(clean);report[@"native_window_preserved"]=@(preserved);
    if(!clean || !preserved)report[@"passed"]=@NO;
    return report;
}
