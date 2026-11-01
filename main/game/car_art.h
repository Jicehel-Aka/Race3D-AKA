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
constexpr uint8_t kLampRear[8] = { 12, 43, 45, 52, 131, 43, 164, 52 };
constexpr uint8_t kLamp3q[CAR_ART_COUNT][8] = {      // same tail lamps on the rigid 3D rotation, 29 degrees (nose to the left)
    { 82, 42, 110, 51, 175, 42, 204, 50 }, { 82, 42, 110, 51, 175, 42, 204, 50 },
    { 82, 42, 110, 51, 175, 42, 204, 50 }, { 82, 42, 110, 51, 175, 42, 204, 50 },
    { 82, 42, 110, 51, 175, 42, 204, 50 }, { 82, 42, 110, 51, 175, 42, 204, 50 },
    { 82, 42, 110, 51, 175, 42, 204, 50 }, { 82, 42, 110, 51, 175, 42, 204, 50 },
};
