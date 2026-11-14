// Native UIKit/SDL ownership check. No guest execution, Vulkan surface or presentation.
#define SDL_MAIN_HANDLED 1
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_metal.h>
#include <cstring>
#include <stdexcept>

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
                report[@"passed"]=@YES;
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
