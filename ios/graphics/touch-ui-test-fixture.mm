// Isolated XCUITest host; no game code or assets.
#import <UIKit/UIKit.h>
#include "ios/services/pad.h"
// Deterministic stand-in for the optional renderer heartbeat.
extern "C" uint64_t oslgpu_presented_frames(void) { return 4321; }
extern "C" bool osl_touch_attach(UIWindow *);
extern "C" void osl_touch_detach(void);
extern "C" void osl_touch_snapshot(PadSample *);
@interface TouchUITestScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) NSTimer *timer;
@property(nonatomic) BOOL sawCross;
@property(nonatomic) BOOL sawMove;
@end
@implementation TouchUITestScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *vc=[UIViewController new];vc.view.backgroundColor=UIColor.darkGrayColor;self.window.rootViewController=vc;
    self.status=[[UILabel alloc] initWithFrame:CGRectMake(20,40,1000,40)];self.status.textColor=UIColor.whiteColor;self.status.accessibilityIdentifier=@"touch-status";self.status.isAccessibilityElement=YES;[vc.view addSubview:self.status];
    [self.window makeKeyAndVisible];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(1*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        BOOL attached=osl_touch_attach(self.window);
        if(!attached){self.status.text=@"attach-failed";return;}
        self.timer=[NSTimer scheduledTimerWithTimeInterval:.05 repeats:YES block:^(NSTimer *timer){
            (void)timer;PadSample p{};osl_touch_snapshot(&p);
            if(p.buttons&BTN_CROSS)self.sawCross=YES;
            if(p.left_x>.55f)self.sawMove=YES;
            UIEdgeInsets s=self.window.safeAreaInsets;CGRect b=self.window.bounds;
            self.status.text=[NSString stringWithFormat:@"ready;%.0f;%.0f;%.0f;%.0f;%.0f;%.0f;%d;%d",b.size.width,b.size.height,s.left,s.top,s.right,s.bottom,self.sawCross,self.sawMove];
        }];
    });
}
- (void)sceneDidDisconnect:(UIScene *)scene { (void)scene;[self.timer invalidate];self.timer=nil;osl_touch_detach(); }
@end
@interface TouchUITestApp : UIResponder <UIApplicationDelegate>
@end
@implementation TouchUITestApp
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)app;(void)options;UISceneConfiguration *c=[[UISceneConfiguration alloc] initWithName:@"TouchUITest" sessionRole:session.role];c.delegateClass=TouchUITestScene.class;return c;
}
@end
int main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(TouchUITestApp.class));}}
