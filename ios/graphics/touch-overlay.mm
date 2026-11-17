// Development overlay adapted from OpenCrown ios/platform/platform.mm (9ca8945).
// UIKit is main-thread only; the guest samples the mutex-protected value object.
#import <UIKit/UIKit.h>
#include "ios/services/touch-controls.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Optional automation-only heartbeat; standalone fixtures may omit the renderer.
extern "C" uint64_t oslgpu_presented_frames(void) __attribute__((weak_import));

static OdinTouchControls touchModel;
extern "C" void osl_touch_snapshot(PadSample *out) { if(out)*out=touchModel.sample(); }

@interface OdinTouchOverlay : UIView
@property(nonatomic) BOOL exposesFrameClock;
@end
@implementation OdinTouchOverlay
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.multipleTouchEnabled=YES;self.opaque=NO;self.backgroundColor=UIColor.clearColor;
        self.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        self.accessibilityLabel=@"Game touch controls";
        const char *clock=std::getenv("OSL_TOUCH_FRAME_CLOCK");
        self.exposesFrameClock=clock && !std::strcmp(clock,"1");
        if(self.exposesFrameClock){self.isAccessibilityElement=YES;self.accessibilityIdentifier=@"touch-frame-clock";}
    }
    return self;
}
- (NSString *)accessibilityValue {
    if(!self.exposesFrameClock)return [super accessibilityValue];
    if(!oslgpu_presented_frames)return @"unavailable";
    return [NSString stringWithFormat:@"%llu",(unsigned long long)oslgpu_presented_frames()];
}
- (void)layoutSubviews {
    [super layoutSubviews];UIEdgeInsets s=self.safeAreaInsets;
    touchModel.configure(self.bounds.size.width,self.bounds.size.height,{float(s.left),float(s.top),float(s.right),float(s.bottom)});
    [self setNeedsDisplay];
}
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)event {
    (void)event;auto l=touchModel.layout();if(!l.valid)return NO;
    for(int i=0;i<OdinTouchControls::Count;++i){auto c=static_cast<OdinTouchControls::Control>(i);auto q=l.centre(c);if(std::hypot(p.x-q.x,p.y-q.y)<l.radius(c))return YES;}
    return NO;
}
- (void)drawRect:(CGRect)rect {
    (void)rect;auto l=touchModel.layout();if(!l.valid)return;
    NSArray<NSString *> *labels=@[@"MOVE",@"LOOK",@"×",@"○",@"△",@"□",@"L1",@"R1",@"MENU",@"PAD",@"D-PAD",@"L2",@"R2",@"L3",@"R3"];
    for(int i=0;i<OdinTouchControls::Count;++i){
        auto c=static_cast<OdinTouchControls::Control>(i);auto p=l.centre(c);CGFloat radius=l.unit*(i<2 ? 1.f : i==OdinTouchControls::Dpad ? .7f : .5f);
        UIBezierPath *path=[UIBezierPath bezierPathWithOvalInRect:CGRectMake(p.x-radius,p.y-radius,2*radius,2*radius)];
        [[UIColor colorWithWhite:.12 alpha:.22] setFill];[path fill];[[UIColor colorWithWhite:1 alpha:.45] setStroke];[path stroke];
        NSDictionary *attributes=@{NSFontAttributeName:[UIFont systemFontOfSize:i<2||i>7 ? 10 : 19 weight:UIFontWeightMedium],NSForegroundColorAttributeName:[UIColor colorWithWhite:1 alpha:.8]};
        CGSize size=[labels[i] sizeWithAttributes:attributes];[labels[i] drawAtPoint:CGPointMake(p.x-size.width/2,p.y-size.height/2) withAttributes:attributes];
    }
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;for(UITouch *t in touches){CGPoint p=[t locationInView:self];touchModel.begin(reinterpret_cast<uintptr_t>((__bridge void *)t),{float(p.x),float(p.y)});}
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;for(UITouch *t in touches){CGPoint p=[t locationInView:self];touchModel.move(reinterpret_cast<uintptr_t>((__bridge void *)t),{float(p.x),float(p.y)});}
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;for(UITouch *t in touches)touchModel.end(reinterpret_cast<uintptr_t>((__bridge void *)t));
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self touchesEnded:touches withEvent:event]; }
@end
static OdinTouchOverlay *overlay;
extern "C" bool osl_touch_attach(UIWindow *window) {
    if(!NSThread.isMainThread || !window || overlay)return false;
    overlay=[[OdinTouchOverlay alloc] initWithFrame:window.bounds];
    [window addSubview:overlay];[overlay setNeedsLayout];[overlay layoutIfNeeded];
    if(!touchModel.layout().valid){[overlay removeFromSuperview];overlay=nil;return false;}
    touchModel.enabled(true);touchModel.active(true);return true;
}
extern "C" void osl_touch_deactivate(void) { touchModel.active(false); }
extern "C" void osl_touch_detach(void) {
    if(!NSThread.isMainThread)return;
    touchModel.active(false);touchModel.enabled(false);[overlay removeFromSuperview];overlay=nil;
}
