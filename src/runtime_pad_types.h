#ifndef OSL_RUNTIME_PAD_TYPES_H
#define OSL_RUNTIME_PAD_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
#define OSL_PAD_ASSERT static_assert
#else
#define OSL_PAD_ASSERT _Static_assert
#endif
enum {
    BTN_L3=0x2, BTN_R3=0x4, BTN_OPTIONS=0x8, BTN_UP=0x10, BTN_RIGHT=0x20, BTN_DOWN=0x40, BTN_LEFT=0x80,
    BTN_L2=0x100, BTN_R2=0x200, BTN_L1=0x400, BTN_R1=0x800, BTN_TRIANGLE=0x1000, BTN_CIRCLE=0x2000,
    BTN_CROSS=0x4000, BTN_SQUARE=0x8000, BTN_TOUCHPAD=0x100000,
};
typedef struct { uint16_t x, y; uint8_t id, reserve[3]; } PadTouch;
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2, analog_padding[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserve[3];
    uint32_t touch_held_time;
    PadTouch touches[2];
    uint8_t connected, pad0[3];
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t connected_count, reserve[2], unique_length, unique[12];
} PadData;
typedef struct {
    float pixel_density; uint16_t resolution_x, resolution_y;
    uint8_t dead_zone_left, dead_zone_right, connection_type, connected_count;
    uint8_t connected, pad[3];
    int32_t device_class;
    uint8_t reserve[8];
} ControllerInfo;
OSL_PAD_ASSERT(sizeof(PadData)==120,"OrbisPadData layout");
OSL_PAD_ASSERT(sizeof(PadTouch)==8,"OrbisPadTouch layout");
OSL_PAD_ASSERT(__builtin_offsetof(PadData,touches)==60,"OrbisPadData touch offset");
OSL_PAD_ASSERT(__builtin_offsetof(PadData,timestamp)==80,"OrbisPadData timestamp offset");
OSL_PAD_ASSERT(sizeof(ControllerInfo)==28,"OrbisPadControllerInformation layout");


#undef OSL_PAD_ASSERT
#endif
