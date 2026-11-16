#pragma once
#include "../../src/runtime_pad_types.h"
#include <cstdint>
#include <functional>
#include <memory>

struct PadSample {
    bool connected=false;
    uint64_t identity=0;
    uint32_t buttons=0;
    float left_x=0,left_y=0,right_x=0,right_y=0,l2=0,r2=0;
};
struct PadReadStats {
    uint64_t total=0, primary=0, fallback=0, disconnected=0;
    // Subset of primary reads containing a connected virtual overlay, and
    // of those, reads with a button, non-neutral stick or trigger.
    uint64_t touch=0, touch_active=0;
};
class MobilePad {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    // An injected source is used only by deterministic backend tests. The app
    // uses the default GameController source, without discovering/pairing UI.
    // An explicit startup-test fallback supplies input only without a primary pad.
    explicit MobilePad(std::function<PadSample(unsigned)> source={},
                       std::function<PadSample(unsigned)> fallback={});
    ~MobilePad();
    int32_t initialize();
    int32_t open(int32_t user,int32_t type,int32_t index);
    int32_t check(int32_t handle);
    int32_t info(int32_t handle,ControllerInfo *out);
    int32_t read(int32_t handle,PadData *out);
    // OS-dialog input is sampled without opening/consuming a game pad handle.
    // Call only from a guest worker with guest state/memory leases released.
    uint32_t dialogButtons();
    int32_t vibration(int32_t handle,const uint8_t *motors);
    uint64_t fallbackReads() const;
    uint64_t fallbackActiveReads() const;
    PadReadStats readStats() const;
};
