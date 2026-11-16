#pragma once
#import <Foundation/Foundation.h>

// Main-thread lease for an app-owned idle timer setting. The host owns the
// target, so leaving a game restores the exact setting it had on entry.
@protocol OslIdleTimerTarget <NSObject>
@property(nonatomic, getter=isIdleTimerDisabled) BOOL idleTimerDisabled;
@end

class OslIdleTimerLease {
    __strong id<OslIdleTimerTarget> target_;
    BOOL previous_;
    bool active_;
public:
    explicit OslIdleTimerLease(id<OslIdleTimerTarget> target)
        : target_(target), previous_(target.idleTimerDisabled), active_(true) {
        target_.idleTimerDisabled=YES;
    }
    OslIdleTimerLease(const OslIdleTimerLease &)=delete;
    OslIdleTimerLease &operator=(const OslIdleTimerLease &)=delete;
    void release() {
        if (!active_) return;
        target_.idleTimerDisabled=previous_;
        active_=false;
    }
    ~OslIdleTimerLease() { release(); }
};
