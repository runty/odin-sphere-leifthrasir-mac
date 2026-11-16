// Standalone physical-device fixture; does not load Odin assets or guest code.
#import <UIKit/UIKit.h>
#include "ios/services/pad.h"
extern "C" bool osl_touch_attach(UIWindow *);
extern "C" void osl_touch_deactivate(void);
extern "C" void osl_touch_detach(void);
extern "C" void osl_touch_snapshot(PadSample *);
@interface TouchFixtureSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation TouchFixtureSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *vc=[UIViewController new];vc.view.backgroundColor=[UIColor colorWithRed:.14 green:.20 blue:.29 alpha:1];
    self.window.rootViewController=vc;[self.window makeKeyAndVisible];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(1*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        PadSample before{};osl_touch_snapshot(&before);
        const bool attached=osl_touch_attach(self.window);[self.window layoutIfNeeded];
        PadSample attachedSample{};osl_touch_snapshot(&attachedSample);
        UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithSize:self.window.bounds.size];
        __block BOOL rendered=NO;
        UIImage *image=[renderer imageWithActions:^(UIGraphicsImageRendererContext *context){(void)context;rendered=[self.window drawViewHierarchyInRect:self.window.bounds afterScreenUpdates:YES];}];
        NSString *dir=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        BOOL saved=[UIImagePNGRepresentation(image) writeToFile:[dir stringByAppendingPathComponent:@"touch-layout.png"] atomically:YES];
        UIEdgeInsets insets=self.window.safeAreaInsets;CGRect bounds=self.window.bounds;
        osl_touch_deactivate();PadSample inactive{};osl_touch_snapshot(&inactive);osl_touch_detach();PadSample detached{};osl_touch_snapshot(&detached);
        NSDictionary *result=@{@"passed":@(!before.connected && attached && attachedSample.connected && !inactive.connected && !detached.connected && rendered && saved),@"attached":@(attached),@"rendered":@(rendered),@"snapshot_saved":@(saved),@"width":@(bounds.size.width),@"height":@(bounds.size.height),@"safe_left":@(insets.left),@"safe_top":@(insets.top),@"safe_right":@(insets.right),@"safe_bottom":@(insets.bottom),@"before_connected":@(before.connected),@"attached_connected":@(attachedSample.connected),@"inactive_connected":@(inactive.connected),@"detached_connected":@(detached.connected),@"physical_touch_verified":@NO,@"game_booted":@NO};
        NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
        [data writeToFile:[dir stringByAppendingPathComponent:@"touch-fixture.json"] atomically:YES];
        NSLog(@"TouchFixture:%@",result);
    });
}
@end
@interface TouchFixtureDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation TouchFixtureDelegate
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)app;(void)options;UISceneConfiguration *config=[[UISceneConfiguration alloc] initWithName:@"TouchFixture" sessionRole:session.role];config.delegateClass=TouchFixtureSceneDelegate.class;return config;
}
@end
int main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(TouchFixtureDelegate.class));}}
