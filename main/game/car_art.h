// car_art.h - rear-view car pictures imported from an art sheet (tools/import_car_art.py)
#pragma once
#include <stdint.h>
constexpr int CAR_ART_COUNT = 8, CAR_ART_W = 176, CAR_ART_H = 84;
constexpr int CAR_ART3Q_W = 224;          // three-quarter pictures (nose to the left, mirrored for the other side)
constexpr uint16_t CAR_ART_KEY = 0xF81F;       // transparent
extern const uint16_t kCarArt[CAR_ART_COUNT][CAR_ART_W * CAR_ART_H];
extern const uint16_t kCarArt3q[CAR_ART_COUNT][CAR_ART3Q_W * CAR_ART_H];
