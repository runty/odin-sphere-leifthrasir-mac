#include "touch-controls.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>
#include <cstdio>
static void exercise(float w,float h,OdinTouchControls::Insets safe) {
    using T=OdinTouchControls;T model;model.configure(w,h,safe);model.enabled(true);model.active(true);
    auto l=model.layout();assert(l.valid && l.unit>0);
    constexpr uint32_t masks[T::Count]={0,0,BTN_CROSS,BTN_CIRCLE,BTN_TRIANGLE,BTN_SQUARE,BTN_L1,BTN_R1,BTN_OPTIONS,BTN_TOUCHPAD,0,BTN_L2,BTN_R2,BTN_L3,BTN_R3};
    for(int i=0;i<T::Count;++i){auto c=static_cast<T::Control>(i);auto p=l.centre(c);assert(p.x>=safe.left && p.x<=w-safe.right && p.y>=safe.top && p.y<=h-safe.bottom);assert(model.begin(i+1,p));assert(!model.begin(i+1,p));if(masks[i])assert(model.sample().buttons==masks[i]);model.end(i+1);}
    auto left=l.centre(T::Move),cross=l.centre(T::Cross);assert(model.begin(101,left));assert(model.begin(102,cross));
    model.move(101,{left.x+l.unit*3,left.y});auto p=model.sample();assert(p.connected && p.left_x>0.999f && std::abs(p.left_y)<1e-6 && (p.buttons&BTN_CROSS));
    model.end(102);p=model.sample();assert(!(p.buttons&BTN_CROSS) && p.left_x>0.999f);model.active(false);p=model.sample();assert(!p.connected);model.active(true);p=model.sample();assert(p.connected && p.left_x==0 && p.buttons==0);
    auto d=l.centre(T::Dpad);assert(model.begin(103,d));model.move(103,{d.x-l.unit,d.y-l.unit});p=model.sample();assert((p.buttons&(BTN_LEFT|BTN_UP))==(BTN_LEFT|BTN_UP));model.end(103);
    auto trigger=l.centre(T::L2);assert(model.begin(104,trigger));p=model.sample();assert((p.buttons&BTN_L2) && p.l2==1);model.end(104);
    assert(!model.begin(200,{NAN,0}));model.enabled(false);assert(!model.sample().connected);
}
int main(){
    exercise(1366,1024,{24,24,24,20});exercise(852,393,{47,0,47,21});exercise(393,852,{0,47,0,34});exercise(568,320,{0,0,0,0});
    OdinTouchControls model;model.configure(852,393,{47,0,47,21});model.enabled(true);model.active(true);auto l=model.layout();auto p=l.centre(OdinTouchControls::Move);
    std::thread writer([&]{for(int i=0;i<20000;++i){model.begin(1,p);model.move(1,{p.x+l.unit,p.y});model.end(1);}});
    std::thread reader([&]{for(int i=0;i<20000;++i){auto s=model.sample();assert(s.connected && std::isfinite(s.left_x) && std::abs(s.left_x)<=1);}});
    writer.join();reader.join();model.configure(0,0,{});assert(!model.sample().connected);
    PadSample physical;physical.connected=true;physical.identity=7;physical.buttons=BTN_CIRCLE;physical.left_x=.25f;physical.l2=.2f;
    PadSample touch;touch.connected=true;touch.identity=99;touch.buttons=BTN_CROSS;touch.left_x=-.75f;touch.l2=.9f;
    auto merged=osl_merge_touch(physical,touch);assert(merged.connected && merged.identity==7 && merged.buttons==(BTN_CIRCLE|BTN_CROSS) && merged.left_x==-.75f && merged.l2==.9f);
    physical.left_x=NAN;merged=osl_merge_touch(physical,touch);assert(merged.left_x==-.75f);
    physical.connected=false;merged=osl_merge_touch(physical,touch);assert(merged.identity==99 && merged.connected);
    touch.connected=false;assert(!osl_merge_touch(physical,touch).connected);
    std::puts("Touch geometry, multitouch, cancellation, safe areas and concurrent snapshots passed");
}
