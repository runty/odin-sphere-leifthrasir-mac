#import <Metal/Metal.h>
#import <TargetConditionals.h>
#import <UIKit/UIKit.h>
#import <sys/utsname.h>
#import <unistd.h>

extern "C" const char *osl_mobile_cpu_report();
#if OSL_RENDERER_SESSION
extern "C" void osl_mobile_renderer_begin(UIWindowScene *,NSString *,void (^)(NSDictionary *));
#endif
extern "C" NSDictionary *osl_mobile_vm_report();
#if OSL_SDL_SURFACE
extern "C" NSDictionary *osl_mobile_surface_report(UIWindowScene *);
#endif
#if OSL_GPU_PROBE
extern "C" NSDictionary *osl_mobile_gpu_report();
#endif
#if OSL_GAME_STARTUP
extern "C" const char *osl_mobile_game_report(const char *);
extern "C" NSDictionary *osl_mobile_memory_report();
#endif

@interface OdinProbeController : UIViewController
@end
@implementation OdinProbeController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Odin Sphere Startup Probe";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView *text = [UITextView new];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    text.text = @"Checking translated routines and guest startup.";
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:text];
    [NSLayoutConstraint activateConstraints:@[
        [text.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [text.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [text.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12]]];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction target:self action:@selector(exportDiagnostics:)];
    self.navigationItem.rightBarButtonItem.enabled = NO;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        NSInteger count = [defaults integerForKey:@"launchCount"] + 1;
        [defaults setInteger:count forKey:@"launchCount"];
        const char *report = osl_mobile_cpu_report();
        NSData *cpuData = [NSData dataWithBytes:report length:strlen(report)];
        NSDictionary *cpu = [NSJSONSerialization JSONObjectWithData:cpuData options:0 error:NULL] ?: @{@"failed": @1};
        NSDictionary *game = @{@"game_booted": @NO, @"status": @"startup library not built"};
#if OSL_GAME_STARTUP
        NSString *image = [[documents stringByAppendingPathComponent:@"Guest"] stringByAppendingPathComponent:@"image.bin"];
        if ([NSFileManager.defaultManager fileExistsAtPath:image]) {
            // A developer launch may compare checked virtual and identity
            // mappings on the same physical device. Normal launches retain
            // their platform default; neither mode enables a game renderer.
            const char *mapping = getenv("OSL_GUEST_IDENTITY");
            if (!mapping || (strcmp(mapping, "0") && strcmp(mapping, "1"))) {
#if TARGET_OS_SIMULATOR
                setenv("OSL_GUEST_IDENTITY", "0", 1);
#else
                setenv("OSL_GUEST_IDENTITY", "1", 1);
#endif
            }
            const char *startup = osl_mobile_game_report(image.fileSystemRepresentation);
            NSData *startupData = [NSData dataWithBytes:startup length:strlen(startup)];
            game = [NSJSONSerialization JSONObjectWithData:startupData options:0 error:NULL] ?: @{@"game_booted": @NO, @"error": @"invalid startup report"};
        } else {
            game = @{@"game_booted": @NO, @"status": @"private prepared image has not been copied into Guest"};
        }
#endif
        NSDictionary *memory = @{@"status": @"startup library not built", @"game_frames": @NO};
#if OSL_GAME_STARTUP
        memory = osl_mobile_memory_report();
#endif
        NSDictionary *graphics = @{@"status": @"GPU probe not built", @"game_frames": @NO};
#if OSL_GPU_PROBE
        graphics = osl_mobile_gpu_report();
#endif
        const char *vmProbe = getenv("OSL_VM_PROBE");
        NSDictionary *virtualMemory = vmProbe && !strcmp(vmProbe, "1") ? osl_mobile_vm_report() : @{@"status": @"not requested", @"game_frames": @NO};
        __block NSDictionary *surface = @{@"status": @"SDL surface provider not linked", @"game_frames": @NO};
#if OSL_SDL_SURFACE
        for(unsigned attempt=0;attempt<20;++attempt) {
            dispatch_sync(dispatch_get_main_queue(), ^{ surface=osl_mobile_surface_report(self.view.window.windowScene); });
            if(![surface[@"status"] isEqual:@"waiting_foreground"])break;
            [NSThread sleepForTimeInterval:0.05];
        }
#endif
        NSDictionary *renderer = @{@"status":@"renderer session not linked",@"game_frames":@NO};
#if OSL_RENDERER_SESSION
        dispatch_semaphore_t rendererDone=dispatch_semaphore_create(0);
        __block NSDictionary *rendererResult=nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            osl_mobile_renderer_begin(self.view.window.windowScene,[documents stringByAppendingPathComponent:@"RendererDiagnostics"],^(NSDictionary *result){rendererResult=result;dispatch_semaphore_signal(rendererDone);});
        });
        if(dispatch_semaphore_wait(rendererDone,dispatch_time(DISPATCH_TIME_NOW,45*NSEC_PER_SEC))!=0)
            renderer=@{@"passed":@NO,@"error":@"Renderer session exceeded diagnostic deadline",@"game_frames":@NO};
        else renderer=rendererResult;
#endif
        struct utsname machine; uname(&machine);
        id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
#if TARGET_OS_SIMULATOR
        id metal4 = NSNull.null;
        NSString *metal4_status = @"unavailable in Simulator SDK";
#else
        id metal4 = @([gpu supportsFamily:MTLGPUFamilyMetal4]);
        NSString *metal4_status = @"queried";
#endif
        NSDictionary *diagnostics = @{
            @"build_id": NSBundle.mainBundle.infoDictionary[@"OSLBuildID"] ?: @"unknown",
            @"stage": @"real-module startup experiment; full game boot pending",
            @"game_booted": @NO, @"cpu": cpu, @"machine": @(machine.machine),
            @"game_startup": game, @"graphics": graphics,
            @"memory": memory,
            @"virtual_memory": virtualMemory,
            @"surface": surface,
            @"renderer": renderer,
            @"gpu_driver_sha256": NSBundle.mainBundle.infoDictionary[@"OSLGPUDriver"] ?: @"none",
            @"startup_corpus_sha256": NSBundle.mainBundle.infoDictionary[@"OSLGameCorpus"] ?: @"none",
            @"sdl_archive_sha256": NSBundle.mainBundle.infoDictionary[@"OSLSDLArchive"] ?: @"none",
            @"renderer_archive_sha256": NSBundle.mainBundle.infoDictionary[@"OSLRendererArchive"] ?: @"none",
            @"os": NSProcessInfo.processInfo.operatingSystemVersionString,
            @"page_size": @(getpagesize()), @"launch_count": @(count),
            @"metal_device": gpu.name ?: @"unavailable", @"bc_texture_compression": @(gpu.supportsBCTextureCompression),
            @"metal4_gpu_family": metal4, @"metal4_query_status": metal4_status,
            @"timestamp": @([[NSDate date] timeIntervalSince1970])};
        NSData *json = [NSJSONSerialization dataWithJSONObject:diagnostics options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
        BOOL saved = [json writeToFile:[documents stringByAppendingPathComponent:@"diagnostics.json"] options:NSDataWritingAtomic error:NULL];
        NSString *rendered = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
        dispatch_async(dispatch_get_main_queue(), ^{ text.text = rendered; self.navigationItem.rightBarButtonItem.enabled = saved; });
        NSLog(@"Odin CPU diagnostics saved=%d %@", saved, rendered);
    });
}
- (void)exportDiagnostics:(UIBarButtonItem *)sender {
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSURL *file = [NSURL fileURLWithPath:[documents stringByAppendingPathComponent:@"diagnostics.json"]];
    UIActivityViewController *sheet = [[UIActivityViewController alloc] initWithActivityItems:@[file] applicationActivities:nil];
    sheet.popoverPresentationController.barButtonItem = sender;
    [self presentViewController:sheet animated:YES completion:nil];
}
@end

@interface OdinSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation OdinSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[OdinProbeController new]];
    [self.window makeKeyAndVisible];
}
@end
@interface OdinAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation OdinAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)app; (void)options;
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Odin" sessionRole:session.role];
    config.delegateClass = OdinSceneDelegate.class;
    return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(OdinAppDelegate.class)); }
}
