#import "idle-timer-lease.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>

@interface FakeIdleTimer : NSObject <OslIdleTimerTarget>
@property(nonatomic, getter=isIdleTimerDisabled) BOOL idleTimerDisabled;
@end
@implementation FakeIdleTimer
@end

int main() {
    @autoreleasepool {
        for (BOOL previous : {NO,YES}) {
            FakeIdleTimer *target=[FakeIdleTimer new];target.idleTimerDisabled=previous;
            {
                OslIdleTimerLease lease(target);
                assert(target.idleTimerDisabled==YES);
                lease.release();assert(target.idleTimerDisabled==previous);
                target.idleTimerDisabled=!previous;
                lease.release();assert(target.idleTimerDisabled==!previous);
            }
            assert(target.idleTimerDisabled==!previous);
            target.idleTimerDisabled=previous;
            { OslIdleTimerLease lease(target);assert(target.idleTimerDisabled==YES); }
            assert(target.idleTimerDisabled==previous);
        }
    }
    std::puts("IDLE_TIMER_LEASE: both prior states, idempotent release, and destructor passed");
}
