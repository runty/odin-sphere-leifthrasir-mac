#import <Metal/Metal.h>
#import <UIKit/UIKit.h>
#import <sys/utsname.h>
#import <unistd.h>

extern "C" const char *osl_mobile_cpu_report();

@interface OdinProbeController : UIViewController
@end
@implementation OdinProbeController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Odin Sphere CPU Probe";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView *text = [UITextView new];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    text.text = @"Checking three translated Odin routines. The game is not connected yet.";
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
        struct utsname machine; uname(&machine);
        id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
        NSDictionary *diagnostics = @{
            @"build_id": NSBundle.mainBundle.infoDictionary[@"OSLBuildID"] ?: @"unknown",
            @"stage": @"three real-module routines; full game startup pending",
            @"game_booted": @NO, @"cpu": cpu, @"machine": @(machine.machine),
            @"os": NSProcessInfo.processInfo.operatingSystemVersionString,
            @"page_size": @(getpagesize()), @"launch_count": @(count),
            @"metal_device": gpu.name ?: @"unavailable", @"bc_texture_compression": @(gpu.supportsBCTextureCompression),
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
