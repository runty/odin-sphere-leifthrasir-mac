// Link a strong overlay snapshot against the real weak-fallback MobilePad.
#include "pad.h"
#include "user.h"
#import <Foundation/Foundation.h>
#include <cassert>
#include <cstdio>
static bool active=true;
extern "C" void osl_touch_snapshot(PadSample *out) {
    if(out){*out={};out->connected=true;out->identity=0x1234;if(active){out->buttons=BTN_CROSS;out->left_x=.9f;}}
}
int main() {
    @autoreleasepool {
        osl_aot_user_reset();
        MobilePad merged;assert(merged.initialize()==0);assert(merged.open(1,0,0)==1);
        PadData value{};assert(merged.read(1,&value)==0 && value.connected && (value.buttons&BTN_CROSS));
        auto stats=merged.readStats();assert(stats.touch==1 && stats.touch_active==1 && stats.primary==1);
        active=false;ControllerInfo info{};assert(merged.info(1,&info)==0);
        assert(merged.readStats().touch==1);
        assert(merged.read(1,&value)==0 && value.connected && !(value.buttons&BTN_CROSS));
        stats=merged.readStats();assert(stats.touch==2 && stats.touch_active==1 && stats.primary==2);
        // An injected/script-only primary must not mix in the overlay.
        MobilePad scripted([](unsigned slot){PadSample p;if(!slot){p.connected=true;p.identity=9;p.buttons=BTN_SQUARE;}return p;});
        assert(scripted.initialize()==0);assert(scripted.open(1,0,0)==1);
        assert(scripted.read(1,&value)==0 && value.connected && value.buttons==BTN_SQUARE);
        stats=scripted.readStats();assert(stats.touch==0 && stats.touch_active==0);
        std::puts("Strong overlay symbol merged; injected script remains isolated");
    }
}
