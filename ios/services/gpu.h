#pragma once
#include "../cpu/runtime.h"

// Installed by the native frontend before a bounded guest session starts.
// The context and function table must remain valid until the report returns.
// Default probe builds have no renderer and retain their explicit service gap.
struct MobileGpuServices {
    void *context;
    bool (*call)(void *,const char *symbol,const char *nid,const uint64_t args[6],uint64_t stack,Memory *,uint64_t *result);
    void (*finish)(void *);
};
extern "C" const char *osl_mobile_game_report_with_gpu(const char *,const MobileGpuServices *);
