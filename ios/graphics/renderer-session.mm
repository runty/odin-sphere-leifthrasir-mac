// Main-thread UIKit renderer ownership, with an optional bounded real guest.
#define SDL_MAIN_HANDLED 1
#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "oslgpu.h"
#include "clock.h"
#include "guest-session.h"
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){runtime_log_flush();std::abort();}

@interface OdinRendererSession : NSObject {
    std::thread guestThread;
    OslGuestSession *guestContext;
    UIBackgroundTaskIdentifier backgroundTask;
}
@property(nonatomic,strong) UIWindowScene *scene;
@property(nonatomic,strong) UIWindow *originalWindow;
@property(nonatomic,strong) CADisplayLink *displayLink;
@property(nonatomic,strong) id inactiveObserver;
@property(nonatomic,strong) id activeObserver;
@property(nonatomic,copy) NSString *profile;
@property(nonatomic,copy) NSString *image;
@property(nonatomic,copy) NSString *pendingError;
@property(nonatomic,strong) NSDictionary *guestReport;
@property(nonatomic,copy) void (^completion)(NSDictionary *);
@property(nonatomic) unsigned polls;
@property(nonatomic) uint32_t sdk;
@property(nonatomic) uint32_t guestBudgetMs;
@property(nonatomic) BOOL initialized;
@property(nonatomic) BOOL finished;
@property(nonatomic) BOOL pendingFinish;
@property(nonatomic) BOOL workerRunning;
@property(nonatomic) BOOL faultsInstalled;
@property(nonatomic) BOOL stopAfterBuffers;
@property(nonatomic) BOOL diagnosticStopRequested;
@property(nonatomic) BOOL testGate;
@property(nonatomic) BOOL testGateReported;
@property(nonatomic,copy) NSString *snapshotPath;
@property(nonatomic,copy) NSString *previousSnapshotPath;
@property(nonatomic,copy) NSString *previousSnapshotSource;
@property(nonatomic,copy) NSString *snapshotSource;
@property(nonatomic,copy) NSString *resolvedInputScriptPath;
@property(nonatomic,copy) NSString *previousInputScriptPath;
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
    if(report[@"stage"])NSLog(@"OdinRendererStage:%@",report[@"stage"]);
}
- (void)endBackgroundTask {
    if(backgroundTask!=UIBackgroundTaskInvalid){[UIApplication.sharedApplication endBackgroundTask:backgroundTask];backgroundTask=UIBackgroundTaskInvalid;}
}
- (void)finish:(NSString *)error {
    if(self.finished)return;
    self.pendingFinish=YES;
    if(error && !self.pendingError)self.pendingError=error;
    [self.displayLink invalidate];self.displayLink=nil;
    if(guestContext)osl_guest_session_request_stop(guestContext);
    // No renderer shutdown, signal restoration or context deletion until the
    // worker has returned its report and has actually been joined.
    if(self.workerRunning)return;
    [self endBackgroundTask];
    if(self.initialized && UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){
        [self persist:@{@"stage":@"guest joined; awaiting foreground renderer teardown",@"game_frames":@NO}];return;
    }
    self.finished=YES;
    if(self.inactiveObserver)[NSNotificationCenter.defaultCenter removeObserver:self.inactiveObserver];
    if(self.activeObserver)[NSNotificationCenter.defaultCenter removeObserver:self.activeObserver];
    self.inactiveObserver=nil;self.activeObserver=nil;
    const uint64_t presented=oslgpu_presented_frames();
    const int stopped=oslgpu_shutdown_main_thread();
    if(self.snapshotPath) {
        if(self.previousSnapshotPath)setenv("OSL_FRAME_SNAPSHOT_PATH",self.previousSnapshotPath.fileSystemRepresentation,1);
        else unsetenv("OSL_FRAME_SNAPSHOT_PATH");
        if(self.previousSnapshotSource)setenv("OSL_FRAME_SNAPSHOT_SOURCE",self.previousSnapshotSource.fileSystemRepresentation,1);
        else unsetenv("OSL_FRAME_SNAPSHOT_SOURCE");
    }
    if(self.resolvedInputScriptPath) {
        if(self.previousInputScriptPath)setenv("OSL_INPUT_SCRIPT",self.previousInputScriptPath.fileSystemRepresentation,1);
        else unsetenv("OSL_INPUT_SCRIPT");
    }
    const bool faultsRestored=!self.faultsInstalled || osl_guest_session_faults_end(guestContext);
    const bool contextReleased=!guestContext || osl_guest_session_destroy(guestContext);
    guestContext=nullptr;
    if(self.originalWindow && self.scene.activationState==UISceneActivationStateForegroundActive)[self.originalWindow makeKeyAndVisible];
    const bool released=!SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD);
    const bool restored=self.scene.keyWindow==self.originalWindow;
    NSDictionary *guest=self.guestReport[@"guest"]?:@{},*cleanup=self.guestReport[@"cleanup"]?:@{};
    const bool executed=[guest[@"completed_modules"] unsignedIntValue]==2 && [guest[@"stage"] isEqual:@"eboot entry"];
    const bool guestOK=!self.image || (executed && [guest[@"gpu_adapter_enabled"] boolValue] &&
        [guest[@"fault_address"] unsignedLongLongValue]==0 && [guest[@"workers_started"] unsignedIntValue]==[guest[@"workers_finished"] unsignedIntValue] &&
        [cleanup[@"passed"] boolValue] && [cleanup[@"opened"] unsignedIntValue]==1 && [cleanup[@"buffers_registered"] unsignedIntValue]>=3 &&
        [cleanup[@"mapped_before_reset"] boolValue] && [cleanup[@"unmapped_after_reset"] boolValue]);
    NSMutableDictionary *report=[@{@"passed":@(!self.pendingError && self.initialized && (self.image || self.polls==30) && guestOK && stopped==0 && released && restored && faultsRestored && contextReleased),
        @"renderer_initialized":@(self.initialized),@"main_thread":@(NSThread.isMainThread),@"polls":@(self.polls),@"sdk":@(self.sdk),
        @"shutdown_complete":@(stopped==0),@"sdl_video_gamepad_released":@(released),@"native_window_restored":@(restored),
        @"fault_handlers_restored":@(faultsRestored),@"guest_context_released":@(contextReleased),@"guest_worker_joined":@(!self.workerRunning),
        @"guest_requested":@(self.image!=nil),@"guest_session":self.guestReport?:@{},@"presented_frames":@(presented),
        @"diagnostic_stop_requested":@(self.diagnosticStopRequested),
        @"diagnostic_gate_requested":@(self.testGate),
        @"guest_budget_ms":@(self.guestBudgetMs),
        @"game_executed":@(executed),@"game_frames":@NO,@"stage":@"stopped"} mutableCopy];
    if(self.snapshotPath) {
        report[@"frame_snapshot_name"]=self.snapshotPath.lastPathComponent;
        report[@"frame_snapshot_exists"]=@([NSFileManager.defaultManager fileExistsAtPath:self.snapshotPath]);
        report[@"frame_snapshot_source"]=self.snapshotSource;
    }
    if(self.pendingError)report[@"error"]=self.pendingError;
    [self persist:report];
    void (^completion)(NSDictionary *)=self.completion;self.completion=nil;
    activeSession=nil;completion(report);
}
- (void)tick:(CADisplayLink *)link {
    (void)link;
    if(UIApplication.sharedApplication.applicationState!=UIApplicationStateActive){[self finish:@"App left foreground during bounded renderer session"];return;}
    if(oslgpu_poll_main_thread()!=1){[self finish:@"Renderer window closed during polling"];return;}
    ++self.polls;
    if(self.image && self.testGate && !self.testGateReported && osl_guest_session_gpu_calls(guestContext)>=7){
        self.testGateReported=YES;
        [self persist:@{@"stage":@"guest buffers registered; diagnostic cancellation gate",@"game_frames":@NO}];
    }
    if(self.image && self.stopAfterBuffers && osl_guest_session_gpu_calls(guestContext)>=7){
        self.diagnosticStopRequested=YES;
        [self finish:@"Requested bounded guest stop after GPU callbacks"];return;
    }
    if(!self.image && self.polls==30)[self finish:nil];
}
- (void)startGuest {
    OdinRendererSession *session=self;
    self.workerRunning=YES;
    __weak OdinRendererSession *weakSelf=self;
    backgroundTask=[UIApplication.sharedApplication beginBackgroundTaskWithName:@"Odin bounded guest cleanup" expirationHandler:^{
        [weakSelf finish:@"Background cleanup time expired"];
        [weakSelf endBackgroundTask];
    }];
    try {
        guestThread=std::thread([session]{@autoreleasepool {
            const char *raw=osl_guest_session_run(session->guestContext,session.image.fileSystemRepresentation);
            NSData *data=[NSData dataWithBytes:raw length:strlen(raw)];
            NSDictionary *result=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]?:@{@"error":@"Invalid native guest report"};
            dispatch_async(dispatch_get_main_queue(),^{
                // The callback can be enqueued just before the native thread
                // exits; join it explicitly before touching its GPU context.
                session->guestThread.join();session.workerRunning=NO;
                session.guestReport=result;
                [session finish:nil];
            });
        }});
    } catch(const std::exception &) {self.workerRunning=NO;[self finish:@"Cannot start owned guest worker"];}
}
- (void)start {
    backgroundTask=UIBackgroundTaskInvalid;
    self.originalWindow=self.scene.keyWindow;
    self.guestBudgetMs=2000;
    // Developer-only bounded startup runs. Validate before acquiring signals,
    // initializing SDL or starting a worker; normal launches retain two seconds.
    if(self.image)if(const char *value=getenv("OSL_GUEST_RUN_MS")) {
        uint32_t budget=0;const char *end=value+std::strlen(value);
        const auto parsed=std::from_chars(value,end,budget);
        if(parsed.ec!=std::errc{} || parsed.ptr!=end || budget<100 || budget>600000){[self finish:@"OSL_GUEST_RUN_MS must be an integer from 100 to 600000"];return;}
        self.guestBudgetMs=budget;
    }
    // A portable developer script path is relative to this app's Documents,
    // independent of its installation UUID. The script itself is loaded and
    // checked by the same bounded C++ adapter used by native discovery.
    if(self.image)if(const char *value=getenv("OSL_INPUT_SCRIPT")) {
        NSString *path=[NSString stringWithUTF8String:value];
        if(!path.length || (!path.isAbsolutePath && [path.pathComponents containsObject:@".."])) {
            [self finish:@"Invalid input script path"];return;
        }
        if(!path.isAbsolutePath) {
            self.previousInputScriptPath=path;
            NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
            self.resolvedInputScriptPath=[documents stringByAppendingPathComponent:path];
            setenv("OSL_INPUT_SCRIPT",self.resolvedInputScriptPath.fileSystemRepresentation,1);
        }
    }
    // Explicit developer-only cancellation fixture. Normal launches do not stop
    // here; the bounded runner retains its own execution deadline/checkpoints.
    const char *stop=getenv("OSL_GUEST_STOP_AFTER_GPU_CALLS");
    self.stopAfterBuffers=stop && !std::strcmp(stop,"7");
    const char *gate=getenv("OSL_GUEST_TEST_GATE");
    self.testGate=gate && !std::strcmp(gate,"after-buffers");
    __weak OdinRendererSession *weakSelf=self;
    self.inactiveObserver=[NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){(void)note;[weakSelf finish:@"App resigned active during bounded renderer session"];}];
    self.activeObserver=[NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){(void)note;if(weakSelf.pendingFinish)[weakSelf finish:nil];}];
    [NSFileManager.defaultManager createDirectoryAtPath:self.profile withIntermediateDirectories:YES attributes:nil error:NULL];
    self.sdk=0x04508000; // Renderer-only diagnostic, never game metadata.
    if(self.image){uint32_t sdk=0;if(!osl_guest_session_sdk(self.image.fileSystemRepresentation,&sdk)){[self finish:@"Prepared image or relocation table validation failed"];return;}self.sdk=sdk;}
    guestContext=osl_guest_session_create_with_budget(self.guestBudgetMs);
    if(!guestContext || !osl_guest_session_faults_begin(guestContext)){[self finish:@"Cannot acquire renderer fault handling"];return;}
    self.faultsInstalled=YES;
    if(self.image && self.testGate && !osl_guest_session_test_gate_after_buffers(guestContext)){[self finish:@"Cannot configure diagnostic cancellation gate"];return;}
    [self persist:@{@"stage":@"starting renderer",@"guest_requested":@(self.image!=nil),@"sdk":@(self.sdk),@"game_frames":@NO}];
    const char *capture=getenv("OSL_CAPTURE_GAME_FRAME");
    const char *displayCapture=getenv("OSL_CAPTURE_DISPLAY_FRAME");
    const bool gameCapture=capture && !std::strcmp(capture,"1");
    const bool fullCapture=displayCapture && !std::strcmp(displayCapture,"1");
    if(self.image && gameCapture && fullCapture){[self finish:@"Choose one snapshot source"];return;}
    if(self.image && (gameCapture || fullCapture)) {
        if(const char *previous=getenv("OSL_FRAME_SNAPSHOT_PATH"))self.previousSnapshotPath=[NSString stringWithUTF8String:previous];
        if(const char *previous=getenv("OSL_FRAME_SNAPSHOT_SOURCE"))self.previousSnapshotSource=[NSString stringWithUTF8String:previous];
        self.snapshotSource=fullCapture ? @"display" : @"game";
        self.snapshotPath=[self.profile stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-frame-%@.png",self.snapshotSource,NSUUID.UUID.UUIDString]];
        setenv("OSL_FRAME_SNAPSHOT_PATH",self.snapshotPath.fileSystemRepresentation,1);
        setenv("OSL_FRAME_SNAPSHOT_SOURCE",self.snapshotSource.UTF8String,1);
    }
    SDL_SetMainReady();oslgpu_register_kernel();
    NSString *settings=[self.profile stringByAppendingPathComponent:@"osl.ini"];
    OslGpuConfig config{"Odin Sphere Bounded Startup",self.image?"CUSA05083":"ODIN_IOS_RENDERER_CHECK",self.profile.fileSystemRepresentation,settings.fileSystemRepresentation,self.sdk,0,960,540};
    if(oslgpu_init(&config)!=0){[self finish:@"Renderer initialization failed"];return;}
    self.initialized=YES;
    if(!self.image){
        using Open=int32_t(*)(int32_t,int32_t,int32_t,const void *);using Close=int32_t(*)(int32_t);
        auto open=reinterpret_cast<Open>(oslgpu_resolve("Up36PTk687E#libSceVideoOut"));
        auto close=reinterpret_cast<Close>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
        const int32_t handle=open?open(1,0,0,nullptr):-1;
        if(handle<=0 || !close || close(handle)!=0){[self finish:@"Native VideoOut open/close failed"];return;}
    }
    [self persist:@{@"stage":@"renderer initialized; polling",@"guest_requested":@(self.image!=nil),@"game_frames":@NO}];
    self.displayLink=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.displayLink.preferredFramesPerSecond=60;
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    if(self.image)[self startGuest];
}
@end
extern "C" void osl_mobile_renderer_begin(UIWindowScene *scene,NSString *profile,NSString *image,void (^completion)(NSDictionary *)) {
    if(!NSThread.isMainThread || !scene || UIApplication.sharedApplication.applicationState!=UIApplicationStateActive || activeSession || SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) {
        completion(@{@"passed":@NO,@"error":@"Renderer session requires an active, unowned main-thread scene",@"game_frames":@NO});return;
    }
    OdinRendererSession *session=[OdinRendererSession new];activeSession=session;
    session.scene=scene;session.profile=profile;session.image=image;session.completion=completion;[session start];
}
