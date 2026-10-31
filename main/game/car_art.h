// car_art.h - rear-view car pictures imported from an art sheet (tools/import_car_art.py)
#pragma once
#include <stdint.h>
constexpr int CAR_ART_COUNT = 8, CAR_ART_W = 176, CAR_ART_H = 84;
constexpr int CAR_ART3Q_W = 224;          // three-quarter pictures (nose to the left, mirrored for the other side)
constexpr int CAR_ART_MID = 5;            // intermediate pictures rear -> three-quarter (width CAR_ART3Q_W, rear view centred)
constexpr uint16_t CAR_ART_KEY = 0xF81F;       // transparent
extern const uint16_t kCarArt[CAR_ART_COUNT][CAR_ART_W * CAR_ART_H];
extern const uint16_t kCarArt3q[CAR_ART_COUNT][CAR_ART3Q_W * CAR_ART_H];
extern const uint16_t kCarArtMid[CAR_ART_COUNT][CAR_ART_MID][CAR_ART3Q_W * CAR_ART_H];
// lamp boxes inside the pictures (x0, y0, x1, y1 for the left and the right lamp); lit by the renderer
constexpr uint8_t kLampRear[8] = { 18, 42, 48, 55, 122, 42, 152, 54 };
constexpr uint8_t kLamp3q[CAR_ART_COUNT][8] = {
    { 78, 41, 110, 52, 172, 41, 203, 51 },    // VIPER
    { 11, 39, 46, 50, 108, 40, 141, 50 },     // BLAZE
    { 80, 40, 115, 51, 177, 40, 212, 50 },    // NOVA
    { 11, 41, 39, 50, 113, 41, 143, 50 },     // RAVEN
    { 12, 39, 45, 49, 110, 39, 147, 48 },     // ACE
    { 13, 41, 44, 49, 111, 40, 148, 49 },     // ZED
    { 11, 39, 46, 50, 108, 40, 141, 50 },     // KAI
    { 78, 41, 110, 52, 172, 41, 203, 51 },    // YOU
};
