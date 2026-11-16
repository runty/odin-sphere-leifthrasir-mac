// Link a strong overlay snapshot against the real weak-fallback MobilePad.
#include "pad.h"
#include "user.h"
#import <Foundation/Foundation.h>
#include <cassert>
#include <cstdio>
extern "C" void osl_touch_snapshot(PadSample *out) {
    if(out){*out={};out->connected=true;out->identity=0x1234;out->buttons=BTN_CROSS;out->left_x=.9f;}
}
int main() {
    @autoreleasepool {
        osl_aot_user_reset();
        MobilePad merged;assert(merged.initialize()==0);assert(merged.open(1,0,0)==1);
        PadData value{};assert(merged.read(1,&value)==0 && value.connected && (value.buttons&BTN_CROSS));
        // An injected/script-only primary must not mix in the overlay.
        MobilePad scripted([](unsigned slot){PadSample p;if(!slot){p.connected=true;p.identity=9;p.buttons=BTN_SQUARE;}return p;});
        assert(scripted.initialize()==0);assert(scripted.open(1,0,0)==1);
        assert(scripted.read(1,&value)==0 && value.connected && value.buttons==BTN_SQUARE);
        std::puts("Strong overlay symbol merged; injected script remains isolated");
    }
}
