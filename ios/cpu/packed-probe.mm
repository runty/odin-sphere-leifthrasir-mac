#import <UIKit/UIKit.h>
#import <TargetConditionals.h>
#import <sys/utsname.h>
#include <cstdint>

extern "C" int osl_packed_simd_verify(uint64_t counts[4]);

@interface PackedProbeController : UIViewController
@end
@implementation PackedProbeController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Odin CPU Comparison";
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView *text = [UITextView new];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    text.text = @"Comparing translated SIMD routines with the native x86 reference…";
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:text];
    [NSLayoutConstraint activateConstraints:@[
        [text.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [text.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [text.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12]]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        uint64_t counts[4]{};
        int status = osl_packed_simd_verify(counts);
        NSDictionary *info = NSBundle.mainBundle.infoDictionary;
        BOOL dimensions = counts[0] == [info[@"OSLRoutines"] unsignedLongLongValue] &&
                          counts[1] == [info[@"OSLVectors"] unsignedLongLongValue] &&
                          counts[2] == counts[0] * counts[1] * [info[@"OSLWordCount"] unsignedLongLongValue];
        struct utsname machine; uname(&machine);
        NSDictionary *report = @{
            @"passed": @(status == 0 && counts[3] == 0 && dimensions),
            @"routines": @(counts[0]), @"vectors": @(counts[1]),
            @"word_checks": @(counts[2]), @"failures": @(counts[3]),
            @"return_and_readonly_input_checks": @(counts[0] * counts[1]),
            @"dimensions_match": @(dimensions), @"game_booted": @NO,
            @"scope": @"Synthetic AOT SIMD comparison; no game, graphics, audio, input or saves",
            @"limitations": info[@"OSLLimitations"] ?: @"missing qualification scope",
            @"machine": @(machine.machine), @"simulator": @(TARGET_OS_SIMULATOR),
            @"os": NSProcessInfo.processInfo.operatingSystemVersionString,
            @"timestamp": @([NSDate.date timeIntervalSince1970]),
            @"oracle_sha256": info[@"OSLOracleSHA"] ?: @"missing",
            @"semantics_sha256": info[@"OSLSemanticsSHA"] ?: @"missing",
            @"lifter_sha256": info[@"OSLLifterSHA"] ?: @"missing",
            @"fixture_manifest_sha256": info[@"OSLFixtureSHA"] ?: @"missing"};
        NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        BOOL saved = [json writeToFile:[documents stringByAppendingPathComponent:@"packed-cpu.json"] options:NSDataWritingAtomic error:NULL];
        dispatch_async(dispatch_get_main_queue(), ^{
            text.text = [NSString stringWithFormat:@"%@\n%@", saved ? @"Report saved" : @"Report write failed", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding]];
        });
    });
}
@end

@interface PackedSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation PackedSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:[PackedProbeController new]];
    [self.window makeKeyAndVisible];
}
@end

@interface PackedAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation PackedAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application; (void)options;
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"CPU Comparison" sessionRole:session.role];
    configuration.delegateClass = PackedSceneDelegate.class;
    return configuration;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(PackedAppDelegate.class)); }
}
