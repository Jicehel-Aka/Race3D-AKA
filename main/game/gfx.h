// gfx.h - software renderer on a 320x240 16-bit framebuffer.
// Pixel format is the AKA native one (see lcd_color_rgb): R in bits 0-4,
// G in bits 5-10, B in bits 11-15.
#pragma once
#include <stdint.h>

namespace gfx {

constexpr int W = 320;
constexpr int H = 240;

extern uint16_t* fb;   // points at the frame being drawn

constexpr uint16_t rgb(int r, int g, int b) {
    return (uint16_t)(((r >> 3) & 31) | (((g >> 2) & 63) << 5) | (((b >> 3) & 31) << 11));
}
inline void unpack(uint16_t c, int& r, int& g, int& b) {
    r = (c & 31) << 3;  r |= r >> 5;
    g = ((c >> 5) & 63) << 2; g |= g >> 6;
    b = ((c >> 11) & 31) << 3; b |= b >> 5;
}
// t = 0 -> a, t = 256 -> b
inline uint16_t lerp(uint16_t a, uint16_t b, int t) {
    int ar = a & 31, ag = (a >> 5) & 63, ab = a >> 11;
    int br = b & 31, bg = (b >> 5) & 63, bb = b >> 11;
    ar += ((br - ar) * t) >> 8;
    ag += ((bg - ag) * t) >> 8;
    ab += ((bb - ab) * t) >> 8;
    return (uint16_t)(ar | (ag << 5) | (ab << 11));
}
inline uint16_t half(uint16_t c) { return (c >> 1) & 0x7BEF; }
// multiply by k/256
inline uint16_t scale(uint16_t c, int k) {
    int r = ((c & 31) * k) >> 8, g = (((c >> 5) & 63) * k) >> 8, b = ((c >> 11) * k) >> 8;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)(r | (g << 5) | (b << 11));
}
// saturating add (glow)
inline uint16_t add(uint16_t a, uint16_t c) {
    int r = (a & 31) + (c & 31), g = ((a >> 5) & 63) + ((c >> 5) & 63), b = (a >> 11) + (c >> 11);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)(r | (g << 5) | (b << 11));
}

inline void pset(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) fb[y * W + x] = c;
}

void clear(uint16_t c);
void hline(int x0, int x1, int y, uint16_t c);            // [x0, x1)
void fillRect(int x, int y, int w, int h, uint16_t c);
void rect(int x, int y, int w, int h, uint16_t c);
void blendRect(int x, int y, int w, int h, uint16_t c, int t);   // t/256 toward c
void darkRect(int x, int y, int w, int h);                         // 50% darken
void fillCircle(int cx, int cy, int r, uint16_t c);
void blendCircle(int cx, int cy, int r, uint16_t c, int t);
void glowCircle(int cx, int cy, int r, uint16_t c);                // additive, soft edge
void line(int x0, int y0, int x1, int y1, uint16_t c);
void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t c);
void fillTri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

// Text (8x8 font, integer scale)
int  textWidth(const char* s, int scale = 1);
void text(int x, int y, const char* s, uint16_t c, int scale = 1);
void textOutline(int x, int y, const char* s, uint16_t c, uint16_t outline, int scale = 1);
// vertical gradient fill + outline + drop shadow (titles, HUD numbers)
void textFancy(int x, int y, const char* s, uint16_t top, uint16_t bottom, uint16_t outline, int scale);
void textCentered(int cx, int y, const char* s, uint16_t c, uint16_t outline, int scale = 1);

} // namespace gfx
