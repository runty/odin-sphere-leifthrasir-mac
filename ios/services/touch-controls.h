#pragma once
// Adapted from runty/OpenCrown-PC ios/platform/platform.mm, revision 9ca8945.
// Pure layout and multitouch state for Odin; no UIKit or guest memory access.
#include "pad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

class OdinTouchControls {
public:
    enum Control { Move, Look, Cross, Circle, Triangle, Square, L1, R1,
                   Menu, Pad, Dpad, L2, R2, L3, R3, Count };
    struct Point { float x=0,y=0; };
    struct Insets { float left=0,top=0,right=0,bottom=0; };
    struct Layout {
        float width=0,height=0,unit=0;
        Insets safe{};
        bool valid=false;
        Point centre(Control c) const {
            const float u=unit,left=safe.left+2*u,right=width-safe.right-2*u;
            const float bottom=height-safe.bottom-1.8f*u;
            const bool compact=(width-safe.left-safe.right)/u<16;
            const float menuY=compact ? bottom-4*u : height-safe.bottom-.7f*u;
            switch(c) {
            case Move:return {left,bottom}; case Look:return {right-2.8f*u,bottom};
            case Cross:return {right,bottom+.85f*u}; case Circle:return {right+.85f*u,bottom};
            case Triangle:return {right,bottom-.85f*u}; case Square:return {right-.85f*u,bottom};
            case L1:return {left,bottom-2*u}; case R1:return {right,bottom-2*u};
            case Menu:return {width/2-u,menuY}; case Pad:return {width/2+u,menuY};
            case Dpad:return {left+2.2f*u,bottom-1.6f*u};
            case L2:return {left+1.3f*u,bottom-2.8f*u}; case R2:return {right-1.3f*u,bottom-2.8f*u};
            case L3:return {left-1.4f*u,bottom+1.25f*u}; case R3:return {right-1.4f*u,bottom+1.2f*u};
            default:return {};
            }
        }
        float radius(Control c) const { return unit*(c<2 ? 1.15f : c==Dpad ? .8f : .63f); }
    };
private:
    struct Contact { uint64_t id=0; Control control=Count; Point position{}; bool used=false; };
    mutable std::mutex mutex_;
    std::array<Contact,16> contacts_{};
    Layout layout_{}; bool enabled_=false,active_=false;
    static constexpr uint64_t virtualIdentity=0x4f534c544f554348ull;
    static bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
    void clearLocked(){contacts_={};}
    static void axes(Point p,Point centre,float unit,float &x,float &y){
        x=(p.x-centre.x)/unit; y=(p.y-centre.y)/unit;
        const float length=std::hypot(x,y); if(length>1){x/=length;y/=length;}
    }
public:
    void configure(float w,float h,Insets safe) {
        std::lock_guard<std::mutex> lock(mutex_);clearLocked();layout_={};
        if(!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(safe.left)||!std::isfinite(safe.right)||!std::isfinite(safe.top)||!std::isfinite(safe.bottom)||w<=0||h<=0||safe.left<0||safe.right<0||safe.top<0||safe.bottom<0)return;
        float width=w-safe.left-safe.right,height=h-safe.top-safe.bottom;
        if(width<240||height<160)return;
        layout_={w,h,std::clamp(std::min(width/14.f,height/9.f),18.f,58.f),safe,true};
    }
    Layout layout() const {std::lock_guard<std::mutex> lock(mutex_);return layout_;}
    void enabled(bool enabled){std::lock_guard<std::mutex> lock(mutex_);enabled_=enabled;if(!enabled)clearLocked();}
    void active(bool active){std::lock_guard<std::mutex> lock(mutex_);active_=active;if(!active)clearLocked();}
    bool begin(uint64_t id,Point p){
        std::lock_guard<std::mutex> lock(mutex_);if(!enabled_||!active_||!layout_.valid||!finite(p))return false;
        for(const auto &c:contacts_)if(c.used&&c.id==id)return false;
        Control chosen=Count;float nearest=INFINITY;
        for(int i=0;i<Count;++i){auto c=static_cast<Control>(i);auto at=layout_.centre(c);float d=std::hypot(p.x-at.x,p.y-at.y);if(d<layout_.radius(c)&&d<nearest){chosen=c;nearest=d;}}
        if(chosen==Count)return false;
        for(const auto &c:contacts_)if(c.used&&c.control==chosen)return false;
        for(auto &c:contacts_)if(!c.used){c={id,chosen,p,true};return true;}
        return false;
    }
    void move(uint64_t id,Point p){std::lock_guard<std::mutex> lock(mutex_);if(!finite(p))return;for(auto &c:contacts_)if(c.used&&c.id==id){c.position=p;return;}}
    void end(uint64_t id){std::lock_guard<std::mutex> lock(mutex_);for(auto &c:contacts_)if(c.used&&c.id==id){c={};return;}}
    PadSample sample() const {
        std::lock_guard<std::mutex> lock(mutex_);PadSample p;
        if(!enabled_||!active_||!layout_.valid)return p;
        p.connected=true;p.identity=virtualIdentity;
        constexpr uint32_t masks[Count]={0,0,BTN_CROSS,BTN_CIRCLE,BTN_TRIANGLE,BTN_SQUARE,BTN_L1,BTN_R1,BTN_OPTIONS,BTN_TOUCHPAD,0,BTN_L2,BTN_R2,BTN_L3,BTN_R3};
        for(const auto &c:contacts_)if(c.used){
            if(c.control==Move||c.control==Look){float x=0,y=0;axes(c.position,layout_.centre(c.control),layout_.unit,x,y);if(c.control==Move){p.left_x=x;p.left_y=y;}else{p.right_x=x;p.right_y=y;}}
            else if(c.control==Dpad){float x=0,y=0;axes(c.position,layout_.centre(Dpad),layout_.unit,x,y);if(x<-.18f)p.buttons|=BTN_LEFT;if(x>.18f)p.buttons|=BTN_RIGHT;if(y<-.18f)p.buttons|=BTN_UP;if(y>.18f)p.buttons|=BTN_DOWN;}
            else {p.buttons|=masks[c.control];if(c.control==L2)p.l2=1;if(c.control==R2)p.r2=1;}
        }
        return p;
    }
};

// OpenCrown's virtual slot 0 combines buttons, prefers the stronger stick,
// and takes the larger trigger. Preserve a physical device's identity so a
// finger touching the screen does not manufacture a controller reconnect.
inline PadSample osl_merge_touch(PadSample physical,const PadSample &touch) {
    if (!touch.connected) return physical;
    if (!physical.connected) return touch;
    auto value=[](float v,float low,float high){return std::isfinite(v) ? std::clamp(v,low,high) : 0.f;};
    auto stronger=[&](float a,float b){a=value(a,-1,1);b=value(b,-1,1);return std::abs(b)>std::abs(a) ? b : a;};
    physical.buttons|=touch.buttons;
    physical.left_x=stronger(physical.left_x,touch.left_x);physical.left_y=stronger(physical.left_y,touch.left_y);
    physical.right_x=stronger(physical.right_x,touch.right_x);physical.right_y=stronger(physical.right_y,touch.right_y);
    physical.l2=std::max(value(physical.l2,0,1),value(touch.l2,0,1));
    physical.r2=std::max(value(physical.r2,0,1),value(touch.r2,0,1));
    return physical;
}
