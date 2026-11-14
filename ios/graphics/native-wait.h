#pragma once
#include "../services/wait_scope.h"

// Call native blocking code without holding the guest state/memory leases.
// Guest faults/cancellation are reported after this scope restores both.
struct NativeGpuWait {
    GuestWaitScope scope;
    explicit NativeGpuWait(GuestWaitScope value):scope(value){scope.before();}
    ~NativeGpuWait(){scope.after();}
    NativeGpuWait(const NativeGpuWait &)=delete;
};
