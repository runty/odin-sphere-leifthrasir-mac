#include "pad-apple.h"
#include "user.h"
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <time.h>
extern "C" void runtime_user_login(int);
static constexpr int32_t invalid=static_cast<int32_t>(0x80920001u),
    invalid_handle=static_cast<int32_t>(0x80920003u), already_open=static_cast<int32_t>(0x80920004u),
    not_initialized=static_cast<int32_t>(0x80920005u), user_not_login=static_cast<int32_t>(0x809b0081u);
static void onMain(dispatch_block_t block) {
    if (NSThread.isMainThread) block(); else dispatch_sync(dispatch_get_main_queue(),block);
}
PadSample sampleAppleController(GCController *controller) {
    PadSample sample;
    GCExtendedGamepad *pad=controller.capture.extendedGamepad;
    if (!pad) return sample;
    sample.connected=true; sample.identity=reinterpret_cast<uintptr_t>((__bridge void *)controller);
    auto button=[&](GCControllerButtonInput *input,uint32_t bit) { if (input.isPressed) sample.buttons|=bit; };
    button(pad.buttonA,BTN_CROSS); button(pad.buttonB,BTN_CIRCLE);
    button(pad.buttonX,BTN_SQUARE); button(pad.buttonY,BTN_TRIANGLE);
    button(pad.leftShoulder,BTN_L1); button(pad.rightShoulder,BTN_R1);
    button(pad.leftThumbstickButton,BTN_L3); button(pad.rightThumbstickButton,BTN_R3);
    button(pad.buttonMenu,BTN_OPTIONS); button(pad.buttonOptions,BTN_TOUCHPAD);
    button(pad.dpad.up,BTN_UP); button(pad.dpad.down,BTN_DOWN);
    button(pad.dpad.left,BTN_LEFT); button(pad.dpad.right,BTN_RIGHT);
    // capture may return the generic extended profile; retain the physical
    // Sony touchpad button when that optional subclass element exists.
    GCExtendedGamepad *physical=controller.extendedGamepad;
    if ([physical respondsToSelector:@selector(touchpadButton)])
        button([(GCDualShockGamepad *)physical touchpadButton],BTN_TOUCHPAD);
    sample.left_x=pad.leftThumbstick.xAxis.value; sample.left_y=-pad.leftThumbstick.yAxis.value;
    sample.right_x=pad.rightThumbstick.xAxis.value; sample.right_y=-pad.rightThumbstick.yAxis.value;
    sample.l2=pad.leftTrigger.value; sample.r2=pad.rightTrigger.value;
    if (sample.l2>0.10f) sample.buttons|=BTN_L2;
    if (sample.r2>0.10f) sample.buttons|=BTN_R2;
    return sample;
}
static float finiteClamp(float value,float low,float high) {
    return std::isfinite(value) ? std::clamp(value,low,high) : 0;
}
static uint8_t axis(float value) {
    value=finiteClamp(value,-1,1);
    value=std::abs(value)<=0.10f ? 0 : (value>0 ? value-0.10f : value+0.10f)/0.90f;
    return static_cast<uint8_t>(std::lround(value*127.5f+127.5f));
}
struct MobilePad::Impl {
    std::mutex mutex;
    std::function<PadSample(unsigned)> source;
    std::function<PadSample(unsigned)> fallback;
    bool initialized=false,opened[4]={},connected[4]={};
    bool used_fallback[4]={};
    uint64_t fallback_reads=0,fallback_active_reads=0;
    uint64_t identities[4]={};
    uint8_t counts[4]={};
    __strong GCController *controllers[4]={};
    __strong CHHapticEngine *engines[4][2]={};
    __strong id<CHHapticPatternPlayer> players[4][2]={};
    explicit Impl(std::function<PadSample(unsigned)> input,std::function<PadSample(unsigned)> backup)
        :source(std::move(input)),fallback(std::move(backup)) {}
    void stop(unsigned slot) {
        for (unsigned motor=0;motor<2;++motor) {
            [players[slot][motor] stopAtTime:0 error:nil]; players[slot][motor]=nil;
            [engines[slot][motor] stopWithCompletionHandler:nil]; engines[slot][motor]=nil;
        }
    }
    ~Impl() { onMain(^{ for (unsigned i=0;i<4;++i) stop(i); }); }
    void refresh() {
        NSArray<GCController *> *available=GCController.controllers;
        for (unsigned i=0;i<4;++i)
            if (controllers[i] && ![available containsObject:controllers[i]]) { stop(i); controllers[i]=nil; }
        for (GCController *controller in available) {
            if (!controller.extendedGamepad) continue;
            bool assigned=false;
            for (unsigned i=0;i<4;++i) assigned|=controllers[i]==controller;
            if (assigned) continue;
            for (unsigned i=0;i<4;++i) if (!controllers[i]) {
                controllers[i]=controller; runtime_user_login(i+1); break;
            }
        }
    }
    PadSample sample(unsigned slot) {
        PadSample result;
        if (source) result=source(slot);
        else {
            __block PadSample physical;
            onMain(^{ refresh(); physical=sampleAppleController(controllers[slot]); });
            result=physical;
        }
        used_fallback[slot]=!result.connected && bool(fallback);
        if (used_fallback[slot]) result=fallback(slot);
        if (result.connected && (!connected[slot] || identities[slot]!=result.identity)) {
            ++counts[slot];runtime_user_login(slot+1);
        }
        connected[slot]=result.connected; identities[slot]=result.identity;
        return result;
    }
    bool handle(int32_t value) const { return value>=1 && value<=4 && opened[value-1]; }
};
MobilePad::MobilePad(std::function<PadSample(unsigned)> source,std::function<PadSample(unsigned)> fallback)
    :impl(std::make_unique<Impl>(std::move(source),std::move(fallback))) {}
MobilePad::~MobilePad()=default;
int32_t MobilePad::initialize() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->source) onMain(^{ impl->refresh(); });
    impl->initialized=true; return 0;
}
int32_t MobilePad::open(int32_t user,int32_t type,int32_t index) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->initialized) return not_initialized;
    if ((type!=0 && type!=2) || index) return invalid;
    int player=runtime_user_player(user);
    if (!player) return user_not_login;
    if (impl->opened[player-1]) return already_open;
    impl->opened[player-1]=true; return player;
}
int32_t MobilePad::check(int32_t handle) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->handle(handle) ? 0 : invalid_handle;
}
int32_t MobilePad::info(int32_t handle,ControllerInfo *out) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->handle(handle)) return invalid_handle;
    if (!out) return invalid;
    auto sample=impl->sample(handle-1);
    std::memset(out,0,sizeof(*out));
    out->pixel_density=44.86f; out->resolution_x=1920; out->resolution_y=943;
    out->dead_zone_left=out->dead_zone_right=2;
    out->connected=sample.connected; out->connected_count=impl->counts[handle-1];
    return 0;
}
int32_t MobilePad::read(int32_t handle,PadData *out) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->handle(handle)) return invalid_handle;
    if (!out) return invalid;
    auto sample=impl->sample(handle-1);
    std::memset(out,0,sizeof(*out));
    out->left_x=out->left_y=out->right_x=out->right_y=128; out->orientation[3]=1;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    out->timestamp=uint64_t(now.tv_sec)*1000000+now.tv_nsec/1000;
    out->connected=sample.connected; out->connected_count=impl->counts[handle-1];
    if (!sample.connected) return 0;
    out->buttons=sample.buttons;
    out->left_x=axis(sample.left_x); out->left_y=axis(sample.left_y);
    out->right_x=axis(sample.right_x); out->right_y=axis(sample.right_y);
    out->l2=std::lround(finiteClamp(sample.l2,0,1)*255);
    out->r2=std::lround(finiteClamp(sample.r2,0,1)*255);
    if (out->buttons&BTN_TOUCHPAD) { out->touch_count=1; out->touches[0].x=480; out->touches[0].y=471; }
    if(impl->used_fallback[handle-1]) {
        ++impl->fallback_reads;
        if(out->buttons || out->left_x!=128 || out->left_y!=128 || out->right_x!=128 ||
           out->right_y!=128 || out->l2 || out->r2)++impl->fallback_active_reads;
    }
    return 0;
}
uint64_t MobilePad::fallbackReads() const {
    std::lock_guard<std::mutex> lock(impl->mutex);return impl->fallback_reads;
}
uint64_t MobilePad::fallbackActiveReads() const {
    std::lock_guard<std::mutex> lock(impl->mutex);return impl->fallback_active_reads;
}
int32_t MobilePad::vibration(int32_t handle,const uint8_t *motors) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->handle(handle)) return invalid_handle;
    if (!motors) return invalid;
    if (impl->source) return 0; // Deterministic tests never vibrate attached hardware.
    onMain(^{
        impl->refresh(); unsigned slot=handle-1;
        GCDeviceHaptics *haptics=impl->controllers[slot].haptics;
        if (!haptics) return; // Optional hardware capability, as on the SDL backend.
        bool separate=[haptics.supportedLocalities containsObject:GCHapticsLocalityLeftHandle] &&
                      [haptics.supportedLocalities containsObject:GCHapticsLocalityRightHandle];
        for (unsigned i=0;i<(separate ? 2u : 1u);++i) {
            [impl->players[slot][i] stopAtTime:0 error:nil]; impl->players[slot][i]=nil;
            float strength=(separate ? motors[i] : std::max(motors[0],motors[1]))/255.0f;
            if (!strength) continue;
            if (!impl->engines[slot][i]) impl->engines[slot][i]=[haptics createEngineWithLocality:
                separate ? (i ? GCHapticsLocalityRightHandle : GCHapticsLocalityLeftHandle) : GCHapticsLocalityDefault];
            CHHapticEngine *engine=impl->engines[slot][i];
            if (![engine startAndReturnError:nil]) continue;
            CHHapticEventParameter *intensity=[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:strength];
            CHHapticEvent *event=[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[intensity] relativeTime:0 duration:0.25];
            CHHapticPattern *pattern=[[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:nil];
            impl->players[slot][i]=[engine createPlayerWithPattern:pattern error:nil];
            [impl->players[slot][i] startAtTime:0 error:nil];
        }
    });
    return 0;
}
