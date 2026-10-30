#include "gfx.h"
#include "font8x8.h"
#include <stdlib.h>

namespace gfx {

uint16_t* fb = nullptr;

void clear(uint16_t c) {
    uint32_t v = c | ((uint32_t)c << 16);
    uint32_t* p = (uint32_t*)fb;
    for (int i = 0; i < W * H / 2; ++i) p[i] = v;
}

void hline(int x0, int x1, int y, uint16_t c) {
    if ((unsigned)y >= (unsigned)H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    uint16_t* p = fb + y * W + x0;
    for (int n = x1 - x0; n > 0; --n) *p++ = c;
}

void fillRect(int x, int y, int w, int h, uint16_t c) {
    if (y < 0) { h += y; y = 0; }
    if (y + h > H) h = H - y;
    for (int j = 0; j < h; ++j) hline(x, x + w, y + j, c);
}

void rect(int x, int y, int w, int h, uint16_t c) {
    hline(x, x + w, y, c);
    hline(x, x + w, y + h - 1, c);
    for (int j = 1; j < h - 1; ++j) { pset(x, y + j, c); pset(x + w - 1, y + j, c); }
}

void blendRect(int x, int y, int w, int h, uint16_t c, int t) {
    int x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w;
    int y0 = y < 0 ? 0 : y, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; ++j) {
        uint16_t* p = fb + j * W;
        for (int i = x0; i < x1; ++i) p[i] = lerp(p[i], c, t);
    }
}

void darkRect(int x, int y, int w, int h) {
    int x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w;
    int y0 = y < 0 ? 0 : y, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; ++j) {
        uint16_t* p = fb + j * W;
        for (int i = x0; i < x1; ++i) p[i] = half(p[i]);
    }
}

void fillCircle(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; ++dy) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) ++dx;
        hline(cx - dx, cx + dx + 1, cy + dy, c);
    }
}

void blendCircle(int cx, int cy, int r, uint16_t c, int t) {
    for (int dy = -r; dy <= r; ++dy) {
        int y = cy + dy;
        if ((unsigned)y >= (unsigned)H) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) ++dx;
        int x0 = cx - dx < 0 ? 0 : cx - dx, x1 = cx + dx + 1 > W ? W : cx + dx + 1;
        uint16_t* p = fb + y * W;
        for (int x = x0; x < x1; ++x) p[x] = lerp(p[x], c, t);
    }
}

void glowCircle(int cx, int cy, int r, uint16_t c) {
    if (r < 1) r = 1;
    int r2 = r * r;
    int inv = (256 << 16) / r2;
    int cr = c & 31, cg = (c >> 5) & 63, cb = c >> 11;
    int y0 = cy - r < 0 ? -cy : -r, y1 = cy + r >= H ? H - 1 - cy : r;
    int x0 = cx - r < 0 ? -cx : -r, x1 = cx + r >= W ? W - 1 - cx : r;
    for (int dy = y0; dy <= y1; ++dy) {
        uint16_t* row = fb + (cy + dy) * W + cx;
        int dy2 = dy * dy;
        for (int dx = x0; dx <= x1; ++dx) {
            int d2 = dx * dx + dy2;
            if (d2 >= r2) continue;
            int k = 256 - ((d2 * inv) >> 16);     // 256 at centre, 0 at edge
            uint16_t p = row[dx];
            int pr = (p & 31) + ((cr * k) >> 8), pg = ((p >> 5) & 63) + ((cg * k) >> 8), pb = (p >> 11) + ((cb * k) >> 8);
            if (pr > 31) pr = 31;
            if (pg > 63) pg = 63;
            if (pb > 31) pb = 31;
            row[dx] = (uint16_t)(pr | (pg << 5) | (pb << 11));
        }
    }
}

void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t c) {
    int h = t / 2;
    for (int oy = -h; oy < t - h; ++oy)
        for (int ox = -h; ox < t - h; ++ox)
            line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c);
}

void fillTri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    // sort by y
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y2 == y0) return;
    for (int y = y0; y <= y2; ++y) {
        float xa = x0 + (float)(x2 - x0) * (y - y0) / (y2 - y0);
        float xb;
        if (y < y1) xb = (y1 == y0) ? x1 : x0 + (float)(x1 - x0) * (y - y0) / (y1 - y0);
        else        xb = (y2 == y1) ? x1 : x1 + (float)(x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { float t = xa; xa = xb; xb = t; }
        hline((int)(xa + 0.5f), (int)(xb + 0.5f) + 1, y, c);
    }
}

// ---------------------------------------------------------------- text
int textWidth(const char* s, int scale) {
    int n = 0;
    while (*s++) ++n;
    return n * 8 * scale;
}

static void glyph(int x, int y, unsigned char ch, uint16_t c, int scale) {
    const unsigned char* g = kFont8x8[ch & 127];
    for (int row = 0; row < 8; ++row) {
        unsigned char bits = g[row];
        for (int col = 0; col < 8; ++col, bits >>= 1)
            if (bits & 1) {
                if (scale == 1) pset(x + col, y + row, c);
                else fillRect(x + col * scale, y + row * scale, scale, scale, c);
            }
    }
}

void text(int x, int y, const char* s, uint16_t c, int scale) {
    for (; *s; ++s, x += 8 * scale) glyph(x, y, (unsigned char)*s, c, scale);
}

void textOutline(int x, int y, const char* s, uint16_t c, uint16_t outline, int scale) {
    int o = scale > 2 ? 2 : 1;
    for (int dy = -o; dy <= o; ++dy)
        for (int dx = -o; dx <= o; ++dx)
            if (dx || dy) text(x + dx, y + dy, s, outline, scale);
    text(x, y, s, c, scale);
}

void textFancy(int x, int y, const char* s, uint16_t top, uint16_t bottom, uint16_t outline, int scale) {
    int o = scale >= 3 ? 2 : 1;
    // drop shadow
    text(x + o + 1, y + o + 2, s, 0, scale);
    for (int dy = -o; dy <= o; ++dy)
        for (int dx = -o; dx <= o; ++dx)
            if (dx || dy) text(x + dx, y + dy, s, outline, scale);
    int hh = 8 * scale;
    for (const char* p = s; *p; ++p, x += 8 * scale) {
        const unsigned char* g = kFont8x8[(unsigned char)*p & 127];
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; ++col, bits >>= 1) {
                if (!(bits & 1)) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    int py = row * scale + sy;
                    uint16_t c = lerp(top, bottom, py * 256 / hh);
                    // little specular band in the upper third
                    if (py == hh / 3 && scale >= 3) c = lerp(c, 0xFFFF, 140);
                    hline(x + col * scale, x + col * scale + scale, y + py, c);
                }
            }
        }
    }
}

void textCentered(int cx, int y, const char* s, uint16_t c, uint16_t outline, int scale) {
    textOutline(cx - textWidth(s, scale) / 2, y, s, c, outline, scale);
}

} // namespace gfx
