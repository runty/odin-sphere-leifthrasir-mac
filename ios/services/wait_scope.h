#pragma once
// Release frontend state/memory leases around a native blocking operation.
// Callers restore both before copying guest outputs or returning to AOT code.
struct GuestWaitScope {
    void *context=nullptr;
    void (*release)(void *)=nullptr;
    void (*acquire)(void *)=nullptr;
    void before()const{if(release)release(context);}
    void after()const{if(acquire)acquire(context);}
};
