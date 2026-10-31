// car_art.h - rear-view car pictures imported from an art sheet (tools/import_car_art.py)
#pragma once
#include <stdint.h>
constexpr int CAR_ART_COUNT = 8, CAR_ART_W = 176, CAR_ART_H = 84;
constexpr uint16_t CAR_ART_KEY = 0xF81F;       // transparent
extern const uint16_t kCarArt[CAR_ART_COUNT][CAR_ART_W * CAR_ART_H];
