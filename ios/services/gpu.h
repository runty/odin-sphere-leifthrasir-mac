#pragma once
#include "../cpu/runtime.h"
#include "wait_scope.h"

// Installed by the native frontend before a bounded guest session starts.
// The context and function table must remain valid until the report returns.
// Default probe builds have no renderer and retain their explicit service gap.
struct MobileGpuServices {
    void *context;
    bool (*call)(void *,const char *symbol,const char *nid,const uint64_t args[6],uint64_t stack,Memory *,uint64_t *result,GuestWaitScope);
    void (*finish)(void *);
    // Optional frontend-owned cancellation. Its lifetime includes the whole
    // report; every guest CPU and bounded host wait observes the same flag.
    std::atomic<bool> *stop_requested=nullptr;
    void *audio_context=nullptr;
    bool (*audio_call)(void *,const char *,const uint64_t[6],Memory &,uint64_t &,GuestWaitScope)=nullptr;
    // Bounded engineering session. Frontends may request 100 ms to ten minutes.
    uint32_t execution_budget_ms=2000;
    // Completed guest presentations, monotonic over a bounded session. Only
    // explicitly enabled startup scripts consume this optional clock.
    uint64_t (*frame_count)(void *)=nullptr;
};
extern "C" const char *osl_mobile_game_report_with_gpu(const char *,const MobileGpuServices *);
