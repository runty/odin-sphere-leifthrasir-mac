#include "pad-apple.h"
#include "user.h"
#import <GameController/GameController.h>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
extern "C" void runtime_user_login(int);
static unsigned checks;
static void require(bool value) {
    if (!value) throw std::runtime_error("pad contract check "+std::to_string(checks+1));
    ++checks;
}
int main() {
    @autoreleasepool {
        try {
            osl_aot_user_reset();
            PadSample input[4];
            MobilePad pad([&](unsigned slot) { return input[slot]; });
            require(uint32_t(pad.open(1,0,0))==0x80920005u);
            require(pad.initialize()==0 && pad.initialize()==0);
            require(uint32_t(pad.open(1,3,0))==0x80920001u);
            require(uint32_t(pad.open(1,0,1))==0x80920001u);
            require(uint32_t(pad.open(2,0,0))==0x809b0081u);
            require(pad.open(1,0,0)==1);
            require(uint32_t(pad.open(1,0,0))==0x80920004u);
            PadData data{};
            ControllerInfo info{};
            require(uint32_t(pad.read(0,&data))==0x80920003u);
            require(uint32_t(pad.read(1,nullptr))==0x80920001u);
            require(uint32_t(pad.info(5,&info))==0x80920003u);
            require(uint32_t(pad.info(1,nullptr))==0x80920001u);
            require(pad.info(1,&info)==0 && !info.connected && info.connected_count==0);
            require(pad.read(1,&data)==0 && !data.connected && !data.buttons);
            require(data.left_x==128 && data.left_y==128 && data.right_x==128 && data.right_y==128);
            require(data.orientation[3]==1 && data.timestamp>0 && data.touch_count==0);
            const uint64_t time=data.timestamp;
            input[0].connected=true; input[0].identity=42;
            input[0].buttons=BTN_CROSS|BTN_OPTIONS|BTN_UP|BTN_TOUCHPAD;
            input[0].left_x=2; input[0].left_y=-2;
            input[0].right_x=std::numeric_limits<float>::quiet_NaN(); input[0].right_y=-0.05f;
            input[0].l2=1; input[0].r2=0.5;
            require(pad.read(1,&data)==0 && data.connected && data.connected_count==1);
            require(data.buttons==input[0].buttons);
            require(data.left_x==255 && data.left_y==0 && data.right_x==128 && data.right_y==128);
            require(data.l2==255 && data.r2==128 && data.timestamp>=time);
            require(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471);
            require(pad.info(1,&info)==0 && info.connected && info.connected_count==1);
            require(info.resolution_x==1920 && info.resolution_y==943 && info.pixel_density>0);
            input[0].connected=false;
            require(pad.read(1,&data)==0 && !data.connected && !data.buttons && data.left_x==128);
            input[0].connected=true; input[0].identity=43;
            require(pad.read(1,&data)==0 && data.connected_count==2);
            input[0].identity=44;
            require(pad.read(1,&data)==0 && data.connected_count==3);
            runtime_user_login(2);
            require(pad.open(2,2,0)==2);
            require(pad.read(2,&data)==0 && !data.connected && data.connected_count==0);
            require(uint32_t(pad.vibration(0,nullptr))==0x80920003u);
            require(uint32_t(pad.vibration(1,nullptr))==0x80920001u);
            uint8_t motors[2]={255,128};
            require(pad.vibration(1,motors)==0); // Injected source never operates real haptics.
            alignas(PadData) uint8_t guarded[sizeof(PadData)+16];
            std::memset(guarded,0x7b,sizeof(guarded));
            require(pad.read(1,reinterpret_cast<PadData *>(guarded+8))==0);
            require(guarded[0]==0x7b && guarded[7]==0x7b && guarded[sizeof(guarded)-8]==0x7b && guarded[sizeof(guarded)-1]==0x7b);
            GCController *controller=[GCController controllerWithExtendedGamepad];
            require(controller.isSnapshot && controller.extendedGamepad!=nil);
            GCExtendedGamepad *gamepad=controller.extendedGamepad;
            [gamepad.buttonA setValue:1]; [gamepad.buttonB setValue:1];
            [gamepad.buttonX setValue:1]; [gamepad.buttonY setValue:1];
            [gamepad.leftShoulder setValue:1]; [gamepad.rightShoulder setValue:1];
            [gamepad.buttonMenu setValue:1]; [gamepad.dpad setValueForXAxis:0 yAxis:1];
            [gamepad.leftThumbstick setValueForXAxis:1 yAxis:1];
            [gamepad.rightThumbstick setValueForXAxis:-1 yAxis:-1];
            [gamepad.leftTrigger setValue:1]; [gamepad.rightTrigger setValue:0.5];
            auto sample=sampleAppleController(controller);
            const uint32_t wanted=BTN_CROSS|BTN_CIRCLE|BTN_SQUARE|BTN_TRIANGLE|BTN_L1|BTN_R1|BTN_OPTIONS|BTN_UP|BTN_L2|BTN_R2;
            if ((sample.buttons&wanted)!=wanted)
                std::fprintf(stderr,"framework snapshot buttons=0x%x expected=0x%x; menu=%d up=%d capture menu=%d up=%d\n",
                             sample.buttons,wanted,gamepad.buttonMenu.isPressed,gamepad.dpad.up.isPressed,
                             controller.capture.extendedGamepad.buttonMenu.isPressed,controller.capture.extendedGamepad.dpad.up.isPressed);
            require(sample.connected && (sample.buttons&wanted)==wanted);
            require(sample.left_x==1 && sample.left_y==-1 && sample.right_x==-1 && sample.right_y==1);
            require(sample.l2==1 && sample.r2==0.5);
            [gamepad.buttonA setValue:0];
            require(!(sampleAppleController(controller).buttons&BTN_CROSS));
            require(!sampleAppleController(nil).connected);
            std::printf("{\"pad_checks\":%u,\"failed\":0,\"physical_controller_verified\":false,\"game_booted\":false}\n",checks);
        } catch (const std::exception &error) {
            std::fprintf(stderr,"%s\n",error.what()); return 1;
        }
    }
}
