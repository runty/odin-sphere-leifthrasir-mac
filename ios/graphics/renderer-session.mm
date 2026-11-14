// Foreground UIKit ownership check for the real renderer. No guest execution.
#define SDL_MAIN_HANDLED 1
#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "oslgpu.h"
#include "clock.h"
#include <cstdio>
#include <cstdlib>

extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){runtime_log_flush();std::abort();}

@interface OdinRendererSession : NSObject
@property(nonatomic,strong) UIWindowScene *scene;
@property(nonatomic,strong) UIWindow *originalWindow;
@property(nonatomic,strong) CADisplayLink *displayLink;
@property(nonatomic,strong) id inactiveObserver;
@property(nonatomic,copy) NSString *profile;
@property(nonatomic,copy) void (^completion)(NSDictionary *);
@property(nonatomic) unsigned polls;
@property(nonatomic) BOOL initialized;
@property(nonatomic) BOOL finished;
@end
static OdinRendererSession *activeSession;
@implementation OdinRendererSession
- (void)persist:(NSDictionary *)report {
    NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    NSMutableDictionary *payload=[report mutableCopy];
    payload[@"build_id"]=NSBundle.mainBundle.infoDictionary[@"OSLBuildID"]?:@"unknown";
    payload[@"renderer_sha256"]=NSBundle.mainBundle.infoDictionary[@"OSLRendererArchive"]?:@"none";
    payload[@"timestamp"]=@([NSDate.date timeIntervalSince1970]);
    NSData *data=[NSJSONSerialization dataWithJSONObject:payload options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
    [data writeToFile:[documents stringByAppendingPathComponent:@"renderer-stages.json"] options:NSDataWritingAtomic error:NULL];
}
- (void)finish:(NSString *)error {
    if(self.finished)return;
    self.finished=YES;
    [self.displayLink invalidate];self.displayLink=nil;
    if(self.inactiveObserver)[NSNotificationCenter.defaultCenter removeObserver:self.inactiveObserver];
    self.inactiveObserver=nil;
    const int stopped=oslgpu_shutdown_main_thread();
    if(self.originalWindow && self.scene.activationState==UISceneActivationStateForegroundActive)[self.originalWindow makeKeyAndVisible];
    const bool released=!SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);
    const bool restored=self.scene.keyWindow==self.originalWindow;
    NSMutableDictionary *report=[@{@"passed":@(!error && self.initialized && self.polls==30 && stopped==0 && released && restored),
        @"renderer_initialized":@(self.initialized),@"main_thread":@(NSThread.isMainThread),@"polls":@(self.polls),
        @"shutdown_complete":@(stopped==0),@"sdl_video_gamepad_released":@(released),@"native_window_restored":@(restored),
        @"game_executed":@NO,@"game_frames":@NO,@"stage":@"stopped"} mutableCopy];
    if(error)report[@"error"]=error;
    [self persist:report];
    void (^completion)(NSDictionary *)=self.completion;self.completion=nil;
    activeSession=nil;
    completion(report);
}
- (void)tick:(CADisplayLink *)link {
    (void)link;
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self finish:@"App left foreground during renderer check"];return;}
    if(oslgpu_poll_main_thread()!=1){[self finish:@"Renderer window closed during polling"];return;}
    if(++self.polls==30)[self finish:nil];
}
- (void)start {
    self.originalWindow=self.scene.keyWindow;
    __weak OdinRendererSession *weakSelf=self;
    self.inactiveObserver=[NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){(void)note;[weakSelf finish:@"App resigned active during renderer check"];}];
    [NSFileManager.defaultManager createDirectoryAtPath:self.profile withIntermediateDirectories:YES attributes:nil error:NULL];
    [self persist:@{@"stage":@"starting renderer",@"game_frames":@NO,@"build_id":NSBundle.mainBundle.infoDictionary[@"OSLBuildID"]?:@"unknown"}];
    SDL_SetMainReady();
    oslgpu_register_kernel();
    NSString *settings=[self.profile stringByAppendingPathComponent:@"osl.ini"];
    OslGpuConfig config{"Odin Sphere Renderer Check","ODIN_IOS_RENDERER_CHECK",self.profile.fileSystemRepresentation,settings.fileSystemRepresentation,0x04508000,0,960,540};
    if(oslgpu_init(&config)!=0){[self finish:@"Renderer initialization failed"];return;}
    self.initialized=YES;
    using Open=int32_t(*)(int32_t,int32_t,int32_t,const void *);using Close=int32_t(*)(int32_t);
    auto open=reinterpret_cast<Open>(oslgpu_resolve("Up36PTk687E#libSceVideoOut"));
    auto close=reinterpret_cast<Close>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
    const int32_t handle=open?open(1,0,0,nullptr):-1;
    if(handle<=0 || !close || close(handle)!=0){[self finish:@"Native VideoOut open/close failed"];return;}
    [self persist:@{@"stage":@"renderer initialized; polling",@"game_frames":@NO}];
    self.displayLink=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.displayLink.preferredFramesPerSecond=60;
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
@end
extern "C" void osl_mobile_renderer_begin(UIWindowScene *scene,NSString *profile,void (^completion)(NSDictionary *)) {
    if(!NSThread.isMainThread || !scene || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || activeSession || SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) {
        completion(@{@"passed":@NO,@"error":@"Renderer check requires an active, unowned main-thread scene",@"game_frames":@NO});return;
    }
    OdinRendererSession *session=[OdinRendererSession new];activeSession=session;
    session.scene=scene;session.profile=profile;session.completion=completion;[session start];
}
