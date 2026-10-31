// car_art.h - rear-view car pictures imported from an art sheet (tools/import_car_art.py)
#pragma once
#include <stdint.h>
constexpr int CAR_ART_COUNT = 8, CAR_ART_W = 112, CAR_ART_H = 58;
extern const uint8_t kCarArtPx[CAR_ART_COUNT][CAR_ART_W * CAR_ART_H];
extern const uint16_t kCarArtPal[CAR_ART_COUNT][16];
