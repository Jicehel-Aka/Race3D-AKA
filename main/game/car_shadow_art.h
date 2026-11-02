// car_shadow_art.h - hand-painted ground shadows of the car pictures (tools/import_car_shadows.py)
#pragma once
#include <stdint.h>
#include "car_art.h"

constexpr int CAR_SHADOW_STAGES = CAR_ART_MID + 2;       // rear view, intermediates, three-quarter view

struct CarShadow {
    int16_t x, y;            // top-left corner, in picture pixels (rear picture scale), relative to the picture's top-left corner
    uint16_t w, h;           // size of the 1-bit mask
    uint32_t off;            // byte offset in kCarShadowBits (rows padded to whole bytes)
};
extern const uint8_t kCarShadowBits[];
extern const CarShadow kCarShadow[CAR_ART_COUNT][CAR_SHADOW_STAGES];
