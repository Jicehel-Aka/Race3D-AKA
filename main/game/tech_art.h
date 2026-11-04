// tech_art.h - pit crew technician pictures (see tech_art.cpp)
#pragma once
#include <stdint.h>
constexpr int TECH_W = 28, TECH_H = 50, TECH_FRAMES = 6;
// frames face right, feet on the bottom row
enum { TECH_IDLE, TECH_WALK_A, TECH_WALK_B, TECH_WORK, TECH_TYRE, TECH_RAISED };
extern const uint16_t kTechPal[16];
extern const uint8_t kTechPx[TECH_FRAMES][TECH_W * TECH_H];
