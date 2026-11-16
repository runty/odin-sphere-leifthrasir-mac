// SPDX-License-Identifier: GPL-2.0-or-later
#import <UIKit/UIKit.h>
#import <TargetConditionals.h>
#import <sys/utsname.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
int osl_page_policy_baseline();
int osl_page_policy_fixed();
@interface PageProbeController : UIViewController
@end
@implementation PageProbeController
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"Odin Page Protection";
    self.view.backgroundColor=UIColor.systemBackgroundColor;
    UITextView *text=[UITextView new]; text.editable=NO;
    text.font=[UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    text.frame=self.view.bounds; text.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    text.text=@"Checking protected memory. No game or audio is running."; [self.view addSubview:text];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        NSString *run=NSUUID.UUID.UUIDString;
        NSString *folder=[[documents stringByAppendingPathComponent:@"page-policy"] stringByAppendingPathComponent:run];
        NSError *error=nil; BOOL created=[NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:&error];
        NSString *raw=[folder stringByAppendingPathComponent:@"raw.log"];
        int original=-1,descriptor=-1,baseline=-1,fixed=-1; BOOL captured=NO;
        if(created) {
            std::fflush(stdout); original=dup(STDOUT_FILENO);
            descriptor=open(raw.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL,0600);
            if(original>=0 && descriptor>=0 && dup2(descriptor,STDOUT_FILENO)>=0) {
                baseline=osl_page_policy_baseline(); fixed=osl_page_policy_fixed();
                std::fflush(stdout); captured=YES;
            }
            if(original>=0) { if(dup2(original,STDOUT_FILENO)<0)captured=NO; close(original); }
            if(descriptor>=0)close(descriptor);
        }
        NSMutableArray *results=[NSMutableArray new];
        NSString *contents=[NSString stringWithContentsOfFile:raw encoding:NSUTF8StringEncoding error:NULL];
        for(NSString *line in [contents componentsSeparatedByString:@"\n"]) {
            if(![line hasPrefix:@"RESULT:"])continue;
            NSData *data=[[line substringFromIndex:7] dataUsingEncoding:NSUTF8StringEncoding];
            id value=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
            if([value isKindOfClass:NSDictionary.class])[results addObject:value];
        }
        BOOL passed=captured && baseline==0 && fixed==0 && results.count==2;
        if(passed) {
            NSDictionary *old=results[0],*current=results[1];
            passed=[old[@"host_page_bytes"] unsignedLongLongValue]==16384 && [current[@"host_page_bytes"] unsignedLongLongValue]==16384 &&
                   [old[@"tracker_page_bytes"] unsignedLongLongValue]==4096 && [current[@"tracker_page_bytes"] unsignedLongLongValue]==16384 &&
                   [old[@"cases"] unsignedLongLongValue]==8 && [current[@"cases"] unsignedLongLongValue]==8 &&
                   [old[@"unresolved_faults"] unsignedLongLongValue]==6 && [current[@"unresolved_faults"] unsignedLongLongValue]==0 &&
                   [old[@"other_failures"] unsignedLongLongValue]==0 && [current[@"other_failures"] unsignedLongLongValue]==0;
        }
        struct utsname machine; uname(&machine); NSDictionary *info=NSBundle.mainBundle.infoDictionary;
        NSDictionary *report=@{@"passed":@(passed),@"run_uuid":run,@"raw_log":raw,@"captured":@(captured),
            @"baseline_exit":@(baseline),@"fixed_exit":@(fixed),@"results":results,
            @"machine":@(machine.machine),@"simulator":@(TARGET_OS_SIMULATOR),
            @"os":NSProcessInfo.processInfo.operatingSystemVersionString,@"timestamp":@([NSDate.date timeIntervalSince1970]),
            @"fixture_sha256":info[@"OSLFixtureSHA"]?:@"missing",@"policy_inputs_sha256":info[@"OSLPolicyInputsSHA"]?:@"missing",
            @"scope":@"Actual host mprotect and renderer header policy; no full renderer, cache-coherence or gameplay acceptance",
            @"game_booted":@NO,@"game_frames":@NO,@"audio_initialized":@NO};
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:NULL];
        BOOL saved=[json writeToFile:[folder stringByAppendingPathComponent:@"result.json"] options:NSDataWritingAtomic error:NULL] &&
                   [json writeToFile:[documents stringByAppendingPathComponent:@"page-policy.json"] options:NSDataWritingAtomic error:NULL];
        dispatch_async(dispatch_get_main_queue(),^{text.text=[NSString stringWithFormat:@"%@\n%@",saved?@"Report saved":@"Report write failed",[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding]];});
    });
}
@end
@interface PageProbeSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation PageProbeSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController=[[UINavigationController alloc] initWithRootViewController:[PageProbeController new]];[self.window makeKeyAndVisible];
}
@end
@interface PageProbeAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation PageProbeAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application;(void)options;UISceneConfiguration *configuration=[[UISceneConfiguration alloc] initWithName:@"Page Protection" sessionRole:session.role];configuration.delegateClass=PageProbeSceneDelegate.class;return configuration;
}
@end
int main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(PageProbeAppDelegate.class));}}
