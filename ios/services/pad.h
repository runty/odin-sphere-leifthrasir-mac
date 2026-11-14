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
class MobilePad {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    // An injected source is used only by deterministic backend tests. The app
    // uses the default GameController source, without discovering/pairing UI.
    explicit MobilePad(std::function<PadSample(unsigned)> source={});
    ~MobilePad();
    int32_t initialize();
    int32_t open(int32_t user,int32_t type,int32_t index);
    int32_t check(int32_t handle);
    int32_t info(int32_t handle,ControllerInfo *out);
    int32_t read(int32_t handle,PadData *out);
    int32_t vibration(int32_t handle,const uint8_t *motors);
};
