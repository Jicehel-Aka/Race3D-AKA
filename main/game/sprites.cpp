// sprites.cpp - all graphics are drawn by code at boot (no asset files).
#include "sprites.h"
#include "gfx.h"
#include "platform.h"
#include "font8x8.h"
#include <math.h>
#include <string.h>
#include <initializer_list>

using gfx::rgb;

Sprite g_sprites[SPR_COUNT];

static const uint8_t SHADOW = 255;

// ------------------------------------------------------------------ canvas
namespace {

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f(); }
};

struct Canvas {
    int w, h;
    uint8_t* p;

    Canvas(Sprite& s, int ww, int hh) : w(ww), h(hh) {
        s.w = (int16_t)ww; s.h = (int16_t)hh;
        s.px = (uint8_t*)plat::allocLarge(ww * hh);
        memset(s.px, 0, ww * hh);
        p = s.px;
    }
    void set(int x, int y, uint8_t i) { if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) p[y * w + x] = i; }
    uint8_t get(int x, int y) const { return ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) ? p[y * w + x] : 0; }
    void rect(int x, int y, int ww, int hh, uint8_t i) {
        for (int j = y; j < y + hh; ++j) for (int k = x; k < x + ww; ++k) set(k, j, i);
    }
    void ellipse(float cx, float cy, float rx, float ry, uint8_t i) {
        for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; ++y)
            for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; ++x) {
                float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
                if (dx * dx + dy * dy <= 1.0f) set(x, y, i);
            }
    }
    void disc(float cx, float cy, float r, uint8_t i) { ellipse(cx, cy, r, r, i); }
    // even-odd scanline polygon fill
    void poly(const float* pts, int n, uint8_t i) {
        float ymin = 1e9f, ymax = -1e9f;
        for (int k = 0; k < n; ++k) { ymin = fminf(ymin, pts[2 * k + 1]); ymax = fmaxf(ymax, pts[2 * k + 1]); }
        for (int y = (int)ymin; y <= (int)ymax; ++y) {
            float yc = y + 0.5f, xs[16]; int m = 0;
            for (int k = 0; k < n && m < 16; ++k) {
                float x0 = pts[2 * k], y0 = pts[2 * k + 1];
                float x1 = pts[2 * ((k + 1) % n)], y1 = pts[2 * ((k + 1) % n) + 1];
                if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc))
                    xs[m++] = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
            }
            for (int a = 0; a < m; ++a) for (int b = a + 1; b < m; ++b) if (xs[b] < xs[a]) { float t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
            for (int a = 0; a + 1 < m; a += 2)
                for (int x = (int)(xs[a] + 0.5f); x < (int)(xs[a + 1] + 0.5f); ++x) set(x, y, i);
        }
    }
    void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint8_t i) {
        float pts[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
        poly(pts, 4, i);
    }
    void thick(float x0, float y0, float x1, float y1, float r, uint8_t i) {
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        int steps = (int)(len * 2) + 1;
        for (int k = 0; k <= steps; ++k) {
            float t = (float)k / steps;
            disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, i);
        }
    }
    void recolor(int x0, int y0, int x1, int y1, uint8_t from, uint8_t to) {
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (get(x, y) == from) set(x, y, to);
    }
    void recolorAny(int x0, int y0, int x1, int y1, uint8_t to) {   // any opaque, non shadow
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { uint8_t c = get(x, y); if (c && c != SHADOW) set(x, y, to); }
    }
    void text(int x, int y, const char* s, uint8_t i, int sc) {
        for (; *s; ++s, x += 8 * sc) {
            const unsigned char* g = kFont8x8[(unsigned char)*s & 127];
            for (int r = 0; r < 8; ++r) {
                unsigned char bits = g[r];
                for (int c = 0; c < 8; ++c, bits >>= 1) if (bits & 1) rect(x + c * sc, y + r * sc, sc, sc, i);
            }
        }
    }
    void textC(int cx, int y, const char* s, uint8_t i, int sc) { text(cx - (int)strlen(s) * 4 * sc, y, s, i, sc); }
    // opaque pixels touching transparency become the outline colour
    void outline(uint8_t o) {
        uint8_t* tmp = (uint8_t*)plat::allocLarge(w * h);
        memcpy(tmp, p, w * h);
        auto clear = [&](int x, int y) {
            if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return true;
            uint8_t c = tmp[y * w + x];
            return c == 0 || c == SHADOW;
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t c = tmp[y * w + x];
                if (!c || c == SHADOW) continue;
                if (clear(x - 1, y) || clear(x + 1, y) || clear(x, y - 1) || clear(x, y + 1)) p[y * w + x] = o;
            }
        plat::freeLarge(tmp);
    }
};

} // namespace

// ------------------------------------------------------------------ cars
// palette: 1 outline 2 body dark 3 body 4 body light 5 glass dark 6 glass light
// 7 tyre 8 tyre light 9 tail 10 tail bright 11 trim 12 plate 13 exhaust 14 stripe
enum { C_OUT = 1, C_BD, C_BM, C_BL, C_GD, C_GL, C_TY, C_TYL, C_TAIL, C_TAILB, C_TRIM, C_PLATE, C_EXH, C_STRIPE };
static const int CAR_W = 112, CAR_H = 58;

static void draw_car_base(Canvas& c) {
    // soft shadow
    c.ellipse(56, 53, 56, 5.5f, SHADOW);
    // tyres
    c.rect(8, 32, 20, 23, C_TY);
    c.rect(84, 32, 20, 23, C_TY);
    for (int y = 34; y < 55; y += 3) { c.rect(9, y, 18, 1, C_TYL); c.rect(85, y, 18, 1, C_TYL); }
    // lower body
    float body[] = { 7, 23, 105, 23, 110, 29, 111, 42, 105, 48, 7, 48, 1, 42, 2, 29 };
    c.poly(body, 8, C_BM);
    c.recolor(0, 23, CAR_W, 25, C_BM, C_BL);
    c.recolor(0, 43, CAR_W, 49, C_BM, C_BD);
    c.recolor(0, 25, 8, 43, C_BM, C_BL);        // lit left flank
    c.recolor(104, 25, CAR_W, 43, C_BM, C_BD);  // shaded right flank
    // cabin
    c.quad(31, 3, 81, 3, 95, 24, 17, 24, C_BM);
    c.recolor(0, 3, CAR_W, 5, C_BM, C_BL);
    c.quad(17, 24, 31, 3, 35, 3, 24, 24, C_BL);     // left pillar (lit)
    c.quad(88, 24, 77, 3, 81, 3, 95, 24, C_BD);     // right pillar (shade)
    // rear window + reflections
    c.quad(36, 6, 76, 6, 86, 21, 26, 21, C_GD);
    c.quad(42, 21, 54, 6, 60, 6, 48, 21, C_GL);
    c.quad(52, 21, 64, 6, 66, 6, 54, 21, C_GL);
    // racing stripes over the body
    for (int y = 3; y < 48; ++y)
        for (int x = 0; x < CAR_W; ++x) {
            bool s = (x >= 49 && x < 54) || (x >= 58 && x < 63);
            uint8_t v = c.get(x, y);
            if (s && (v == C_BM || v == C_BL || v == C_BD)) c.set(x, y, C_STRIPE);
        }
    // rear wing
    c.quad(0, 17, 112, 17, 110, 22, 2, 22, C_BD);
    c.rect(2, 17, 108, 1, C_BL);
    c.rect(28, 22, 5, 5, C_TRIM);
    c.rect(79, 22, 5, 5, C_TRIM);
    // tail lights + centre panel
    c.rect(7, 28, 27, 7, C_TAIL);
    c.rect(78, 28, 27, 7, C_TAIL);
    c.rect(9, 29, 10, 4, C_TAILB);
    c.rect(93, 29, 10, 4, C_TAILB);
    c.rect(36, 28, 40, 7, C_TRIM);
    c.rect(52, 30, 8, 3, C_STRIPE);
    // bumper, plate, diffuser, exhausts
    c.rect(9, 39, 94, 5, C_TRIM);
    c.rect(45, 37, 22, 7, C_PLATE);
    c.rect(48, 39, 4, 3, C_OUT); c.rect(54, 39, 4, 3, C_OUT); c.rect(60, 39, 4, 3, C_OUT);
    for (int x = 42; x < 72; x += 5) c.rect(x, 44, 2, 4, C_TRIM);
    c.ellipse(36, 46, 4.5f, 2.6f, C_EXH); c.ellipse(36, 46, 2.2f, 1.3f, C_OUT);
    c.ellipse(76, 46, 4.5f, 2.6f, C_EXH); c.ellipse(76, 46, 2.2f, 1.3f, C_OUT);
}

static void make_cars() {
    // straight
    {
        Sprite& s = g_sprites[SPR_CAR];
        Canvas c(s, CAR_W, CAR_H);
        draw_car_base(c);
        c.outline(C_OUT);
    }
    // turning frames: shear the upper body (car yawing)
    for (int dir = -1; dir <= 1; dir += 2) {
        Sprite& s = g_sprites[dir < 0 ? SPR_CAR_LEFT : SPR_CAR_RIGHT];
        Canvas c(s, CAR_W, CAR_H);
        Sprite tmpS; Canvas src(tmpS, CAR_W, CAR_H);
        draw_car_base(src);
        for (int y = 0; y < CAR_H; ++y) {
            int off = y < 34 ? (int)lroundf(dir * 6.0f * (34 - y) / 34.0f) : 0;
            for (int x = 0; x < CAR_W; ++x) {
                uint8_t v = src.get(x - off, y);
                if (v) c.set(x, y, v);
            }
        }
        // the side of the car becomes visible on the outer side
        for (int y = 23; y < 46; ++y) {
            if (dir < 0) { for (int k = 104; k < 111; ++k) if (c.get(k, y) && c.get(k, y) != SHADOW) c.set(k, y, C_BD); }
            else         { for (int k = 1;   k < 8;   ++k) if (c.get(k, y) && c.get(k, y) != SHADOW) c.set(k, y, C_BL); }
        }
        c.outline(C_OUT);
        plat::freeLarge(tmpS.px);
    }
    for (int i = SPR_CAR; i <= SPR_CAR_RIGHT; ++i) {
        g_sprites[i].worldW = 700;
        g_sprites[i].hitW = 0.95f;
        g_sprites[i].emissive = (1 << C_TAIL) | (1 << C_TAILB);
    }
}

void car_palette(uint16_t out[16], const Livery& l, bool braking) {
    out[0] = 0;
    out[C_OUT] = rgb(16, 12, 20);
    out[C_BD] = gfx::scale(l.body, 150);
    out[C_BM] = l.body;
    out[C_BL] = gfx::lerp(l.body, 0xFFFF, 100);
    out[C_GD] = rgb(30, 40, 62);
    out[C_GL] = rgb(110, 140, 170);
    out[C_TY] = rgb(26, 24, 28);
    out[C_TYL] = rgb(58, 56, 62);
    out[C_TAIL] = braking ? rgb(255, 40, 40) : rgb(150, 18, 26);
    out[C_TAILB] = braking ? rgb(255, 230, 200) : rgb(220, 60, 60);
    out[C_TRIM] = rgb(44, 44, 52);
    out[C_PLATE] = rgb(236, 232, 210);
    out[C_EXH] = rgb(150, 150, 160);
    out[C_STRIPE] = l.stripe;
    out[15] = 0;
}

// ------------------------------------------------------------------ scenery
// shared indices for vegetation: 1 outline 2 trunk dark 3 trunk 4 trunk light
// 5 leaf dark 6 leaf 7 leaf light / snow 8 accent (coconut / flower)
enum { V_OUT = 1, V_TD, V_TM, V_TL, V_LD, V_LM, V_LL, V_ACC };

static void make_palm() {
    Sprite& s = g_sprites[SPR_PALM];
    Canvas c(s, 100, 176);
    c.ellipse(46, 172, 20, 3.5f, SHADOW);
    for (int y = 172; y >= 44; --y) {
        float t = (172 - y) / 128.0f;
        float cx = 44 + 18 * t * t, hw = 6.0f - 2.5f * t;
        for (int x = (int)(cx - hw); x <= (int)(cx + hw); ++x) {
            float u = (x - (cx - hw)) / (2 * hw);
            c.set(x, y, u < 0.3f ? V_TL : (u < 0.7f ? V_TM : V_TD));
        }
        if ((y % 7) == 0) for (int x = (int)(cx - hw); x <= (int)(cx + hw); ++x) c.set(x, y, V_TD);
    }
    const float tx = 62, ty = 44;
    const float angles[] = { 175, 145, 115, 90, 62, 33, 5 };
    for (int pass = 0; pass < 2; ++pass)
        for (float a : angles) {
            float ar = a * 3.14159f / 180.0f;
            float L = 46 - fabsf(90 - a) * 0.05f;
            for (float t = 0; t <= 1.0f; t += 0.015f) {
                float x = tx + cosf(ar) * L * t, y = ty - sinf(ar) * L * t * 0.75f + 30 * t * t;
                float r = 5.2f * (1 - t) + 1.2f;
                if (pass == 0) {
                    // drooping leaflets underneath
                    c.thick(x, y, x + cosf(ar) * 3, y + r * 1.9f + 2, 0.8f, V_LD);
                } else {
                    c.disc(x, y, r, V_LM);
                    c.disc(x - 0.5f, y - r * 0.45f, r * 0.45f, V_LL);
                }
            }
        }
    c.disc(58, 47, 3.2f, V_ACC); c.disc(64, 48, 3.2f, V_ACC); c.disc(61, 51, 3.0f, V_ACC);
    c.outline(V_OUT);
    s.worldW = 1400; s.hitW = 0.18f;
}

static void make_pine() {
    Sprite& s = g_sprites[SPR_PINE];
    Canvas c(s, 76, 156);
    c.ellipse(38, 152, 26, 3.5f, SHADOW);
    c.rect(33, 128, 10, 25, V_TM);
    c.rect(33, 128, 3, 25, V_TL);
    c.rect(40, 128, 3, 25, V_TD);
    for (int i = 0; i < 5; ++i) {
        float apex = 4 + i * 23.0f, base = apex + 48 - i * 2, hw = 37 - i * 5.5f;
        for (int y = (int)apex; y <= (int)base; ++y) {
            float t = (y - apex) / (base - apex);
            float half = hw * t;
            int zig = ((y + i) % 4 < 2) ? 0 : 2;
            for (int x = (int)(38 - half) - zig; x <= (int)(38 + half) + zig; ++x) {
                uint8_t col = x < 38 - half * 0.15f ? V_LM : V_LD;
                if (t < 0.22f || (x < 38 - half * 0.6f && ((x + y) % 5 == 0))) col = V_LL;
                c.set(x, y, col);
            }
        }
    }
    c.outline(V_OUT);
    s.worldW = 1150; s.hitW = 0.2f;
}

static void make_tree() {
    Sprite& s = g_sprites[SPR_TREE];
    Canvas c(s, 112, 132);
    c.ellipse(56, 128, 38, 4, SHADOW);
    c.quad(50, 128, 62, 128, 60, 70, 52, 70, V_TM);
    c.thick(56, 92, 36, 66, 2.5f, V_TM);
    c.thick(56, 86, 78, 62, 2.5f, V_TM);
    c.recolor(0, 0, 54, 132, V_TM, V_TL);
    Rng r(77);
    struct B { float x, y, rr; } blobs[18];
    for (auto& b : blobs) { b.x = r.range(24, 88); b.y = r.range(18, 74); b.rr = r.range(14, 22); }
    for (auto& b : blobs) c.disc(b.x, b.y, b.rr, V_LD);
    for (auto& b : blobs) c.disc(b.x - b.rr * 0.25f, b.y - b.rr * 0.3f, b.rr * 0.72f, V_LM);
    for (auto& b : blobs) if (b.x < 64 && b.y < 56) c.disc(b.x - b.rr * 0.4f, b.y - b.rr * 0.45f, b.rr * 0.35f, V_LL);
    c.outline(V_OUT);
    s.worldW = 1700; s.hitW = 0.15f;
}

static void make_bush() {
    Sprite& s = g_sprites[SPR_BUSH];
    Canvas c(s, 84, 46);
    c.ellipse(42, 42, 40, 4, SHADOW);
    Rng r(5);
    struct B { float x, y, rr; } blobs[9];
    for (auto& b : blobs) { b.x = r.range(16, 68); b.y = r.range(22, 32); b.rr = r.range(9, 15); }
    for (auto& b : blobs) c.disc(b.x, b.y, b.rr, V_LD);
    for (auto& b : blobs) c.disc(b.x - 2, b.y - 3, b.rr * 0.7f, V_LM);
    for (auto& b : blobs) c.disc(b.x - 4, b.y - 6, b.rr * 0.3f, V_LL);
    for (int k = 0; k < 14; ++k) { int x = (int)r.range(14, 70), y = (int)r.range(14, 36); if (c.get(x, y)) c.rect(x, y, 2, 2, V_ACC); }
    c.outline(V_OUT);
    s.worldW = 1000; s.hitW = 0.6f;
}

// rock: 1 outline 2 dark 3 mid 4 light 7 snow
static void make_rock() {
    Sprite& s = g_sprites[SPR_ROCK];
    Canvas c(s, 76, 54);
    c.ellipse(38, 50, 36, 4, SHADOW);
    float pts[] = { 4, 50, 8, 28, 20, 12, 38, 5, 56, 10, 70, 26, 73, 50 };
    c.poly(pts, 7, 3);
    float lit[] = { 8, 30, 20, 13, 38, 6, 34, 26, 16, 40 };
    c.poly(lit, 5, 4);
    float dk[] = { 52, 50, 58, 30, 70, 27, 73, 50 };
    c.poly(dk, 4, 2);
    c.thick(34, 26, 46, 44, 0.6f, 2);
    c.thick(46, 44, 44, 50, 0.6f, 2);
    for (int y = 0; y < 18; ++y) for (int x = 0; x < 76; ++x) if (c.get(x, y) && c.get(x, y) != SHADOW && y < 12 + ((x * 7) % 5)) c.set(x, y, 7);
    c.outline(1);
    s.worldW = 1000; s.hitW = 0.8f;
}

// billboard: 1 outline 2 post 3 frame 4 panel 5 text 6 accent 7 frame light
static void make_board(SpriteId id, const char* big, int bigScale, const char* small) {
    Sprite& s = g_sprites[id];
    Canvas c(s, 140, 106);
    c.ellipse(70, 102, 60, 3.5f, SHADOW);
    c.rect(26, 60, 8, 43, 2); c.rect(106, 60, 8, 43, 2);
    c.rect(4, 4, 132, 64, 3);
    c.rect(4, 4, 132, 2, 7);
    c.rect(9, 9, 122, 54, 4);
    c.rect(9, 52, 122, 6, 6);
    int bh = 8 * bigScale;
    int ty = small ? 13 : 9 + (43 - bh) / 2;
    c.textC(70, ty, big, 5, bigScale);
    if (small) c.textC(70, ty + bh + 3, small, 6, 1);
    c.outline(1);
    s.worldW = 2300; s.hitW = 0.95f;
    s.emissive = (1 << 5) | (1 << 6);
}

// chevron: 1 outline 2 post 3 board 4 arrows
static void make_chevron() {
    Sprite& s = g_sprites[SPR_CHEVRON];
    Canvas c(s, 58, 58);
    c.ellipse(29, 55, 16, 2.5f, SHADOW);
    c.rect(26, 38, 6, 17, 2);
    c.rect(2, 4, 54, 36, 3);
    for (int k = 0; k < 2; ++k) {
        float x = 10 + k * 20;
        c.quad(x, 9, x + 8, 9, x + 18, 22, x + 10, 22, 4);
        c.quad(x + 10, 22, x + 18, 22, x + 8, 35, x, 35, 4);
    }
    c.outline(1);
    s.worldW = 800; s.hitW = 0.9f;
}

// lamp: 1 outline 2 pole dark 3 pole 4 head 5 light (emissive)
static void make_lamp() {
    Sprite& s = g_sprites[SPR_LAMP];
    Canvas c(s, 60, 180);
    c.ellipse(10, 177, 8, 2, SHADOW);
    c.rect(7, 20, 6, 157, 3);
    c.rect(10, 20, 3, 157, 2);
    c.rect(5, 168, 10, 9, 2);
    for (float t = 0; t <= 1; t += 0.02f) {
        float x = 10 + 34 * t, y = 22 - 12 * sinf(t * 1.6f);
        c.disc(x, y, 1.8f, 3);
    }
    c.rect(36, 10, 20, 6, 4);
    c.rect(38, 16, 16, 3, 5);
    c.outline(1);
    s.worldW = 950; s.hitW = 0.12f;
    s.emissive = 1 << 5;
}

// building: 1 outline 2 wall dark 3 wall 4 wall light 5 window off 6 lit warm 7 lit cool 8 beacon
static void make_building(SpriteId id, uint32_t seed, int bw, int floors, int cols) {
    Sprite& s = g_sprites[id];
    const int H = 236;
    Canvas c(s, bw, H);
    int top = H - floors * 14 - 10;
    c.rect(0, top, bw, H - top, 3);
    c.rect(0, top, 4, H - top, 4);
    c.rect(bw - 6, top, 6, H - top, 2);
    c.rect(2, top - 4, bw - 4, 5, 2);
    c.rect(bw / 2 - 1, top - 22, 2, 18, 2);
    c.rect(bw / 2 - 2, top - 25, 4, 4, 8);
    Rng r(seed);
    int cw = (bw - 12) / cols;
    for (int f = 0; f < floors; ++f)
        for (int k = 0; k < cols; ++k) {
            float v = r.f();
            uint8_t col = v < 0.45f ? 5 : (v < 0.85f ? 6 : 7);
            c.rect(7 + k * cw, top + 8 + f * 14, cw - 4, 8, col);
        }
    c.rect(0, H - 14, bw, 14, 2);
    c.rect(bw / 2 - 12, H - 13, 24, 13, 6);
    c.outline(1);
    s.worldW = 3600.0f * bw / 120.0f; s.hitW = 0.95f;
    s.emissive = (1 << 6) | (1 << 7) | (1 << 8);
}

// gantry: 1 outline 2 pillar 3 pillar light 4 banner 5 text 6 check dark 7 check light
static void make_gantry() {
    Sprite& s = g_sprites[SPR_GANTRY];
    Canvas c(s, 256, 112);
    c.rect(2, 0, 14, 112, 2); c.rect(2, 0, 4, 112, 3);
    c.rect(240, 0, 14, 112, 2); c.rect(240, 0, 4, 112, 3);
    c.rect(0, 2, 256, 40, 4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 32; ++x) {
            c.rect(x * 8, 2 + y * 4, 8, 4, ((x + y) & 1) ? 6 : 7);
            c.rect(x * 8, 34 + y * 4, 8, 4, ((x + y) & 1) ? 7 : 6);
        }
    c.textC(128, 14, "AKA GRAND PRIX", 5, 2);
    c.outline(1);
    s.worldW = 2000 * 2 * 1.32f; s.hitW = 0.0f;
    s.emissive = 1 << 5;
}

void sprites_init() {
    make_cars();
    make_palm();
    make_pine();
    make_tree();
    make_bush();
    make_rock();
    make_board(SPR_BOARD_A, "AKA", 4, "GAMEBUINO");
    make_board(SPR_BOARD_B, "TURBO", 3, "RACING TEAM");
    make_board(SPR_BOARD_C, "NITRO", 3, "PRESS C / R1");
    make_chevron();
    make_lamp();
    make_building(SPR_BUILDING_A, 11, 120, 15, 6);
    make_building(SPR_BUILDING_B, 23, 96, 11, 4);
    make_building(SPR_BUILDING_C, 37, 132, 13, 7);
    make_gantry();
}

// ------------------------------------------------------------------ palettes
// [sprite][theme][16]
static uint16_t s_pal[SPR_COUNT][3][16];
static bool s_palReady = false;

static void set_pal(int id, int theme, std::initializer_list<uint16_t> v) {
    int i = 0;
    for (uint16_t c : v) s_pal[id][theme][i++] = c;
}

static void build_palettes() {
    const uint16_t O = rgb(18, 14, 22);
    for (int t = 0; t < 3; ++t) {
        // vegetation per theme: coast (lush, warm), alpine (cold, snowy), city (night-blue)
        uint16_t ld = t == 0 ? rgb(24, 92, 40) : t == 1 ? rgb(18, 64, 46) : rgb(18, 60, 52);
        uint16_t lm = t == 0 ? rgb(60, 150, 50) : t == 1 ? rgb(34, 104, 62) : rgb(30, 96, 80);
        uint16_t ll = t == 0 ? rgb(150, 210, 70) : t == 1 ? rgb(240, 246, 255) : rgb(70, 150, 120);
        uint16_t acc = t == 0 ? rgb(240, 110, 150) : t == 1 ? rgb(255, 255, 255) : rgb(240, 200, 90);
        set_pal(SPR_PALM, t, { 0, O, rgb(92, 56, 34), rgb(140, 92, 54), rgb(190, 140, 88), ld, lm, t == 1 ? rgb(120, 190, 80) : ll, rgb(110, 70, 30) });
        set_pal(SPR_PINE, t, { 0, O, rgb(70, 44, 30), rgb(110, 70, 44), rgb(150, 100, 64), ld, lm, ll, acc });
        set_pal(SPR_TREE, t, { 0, O, rgb(70, 44, 30), rgb(110, 70, 44), rgb(150, 100, 64), ld, lm, t == 1 ? rgb(90, 160, 80) : ll, acc });
        set_pal(SPR_BUSH, t, { 0, O, 0, 0, 0, ld, lm, t == 1 ? rgb(90, 160, 80) : ll, acc });
        uint16_t snow = t == 1 ? rgb(244, 248, 255) : (t == 0 ? rgb(214, 190, 150) : rgb(130, 130, 150));
        set_pal(SPR_ROCK, t, { 0, O, rgb(84, 80, 92), rgb(130, 126, 138), rgb(180, 176, 186), 0, 0, snow });
        set_pal(SPR_BOARD_A, t, { 0, O, rgb(90, 90, 100), rgb(200, 200, 210), t == 2 ? rgb(20, 10, 40) : rgb(30, 60, 160),
                                   t == 2 ? rgb(0, 255, 240) : rgb(255, 214, 0), t == 2 ? rgb(255, 60, 200) : rgb(255, 255, 255), rgb(250, 250, 255) });
        set_pal(SPR_BOARD_B, t, { 0, O, rgb(90, 90, 100), rgb(40, 40, 48), t == 2 ? rgb(30, 0, 30) : rgb(220, 30, 40),
                                   t == 2 ? rgb(255, 80, 220) : rgb(255, 255, 255), t == 2 ? rgb(255, 240, 80) : rgb(255, 210, 0), rgb(120, 120, 130) });
        set_pal(SPR_BOARD_C, t, { 0, O, rgb(90, 90, 100), rgb(240, 160, 0), t == 2 ? rgb(0, 20, 40) : rgb(20, 20, 30),
                                   t == 2 ? rgb(80, 255, 120) : rgb(255, 140, 0), t == 2 ? rgb(0, 200, 255) : rgb(200, 200, 210), rgb(255, 220, 120) });
        set_pal(SPR_CHEVRON, t, { 0, O, rgb(120, 120, 130), t == 2 ? rgb(255, 60, 160) : rgb(255, 210, 0), rgb(20, 20, 24) });
        set_pal(SPR_LAMP, t, { 0, O, rgb(60, 64, 76), rgb(120, 126, 140), rgb(50, 52, 60), rgb(255, 240, 190) });
        set_pal(SPR_BUILDING_A, t, { 0, O, rgb(30, 30, 58), rgb(48, 48, 84), rgb(70, 70, 110), rgb(24, 26, 44), rgb(255, 214, 120), rgb(140, 220, 255), rgb(255, 40, 40) });
        set_pal(SPR_BUILDING_B, t, { 0, O, rgb(44, 26, 52), rgb(70, 40, 80), rgb(96, 60, 110), rgb(30, 20, 40), rgb(255, 180, 90), rgb(255, 120, 220), rgb(255, 40, 40) });
        set_pal(SPR_BUILDING_C, t, { 0, O, rgb(22, 40, 50), rgb(36, 62, 76), rgb(56, 88, 104), rgb(18, 30, 38), rgb(240, 240, 170), rgb(100, 255, 200), rgb(255, 40, 40) });
        set_pal(SPR_GANTRY, t, { 0, O, rgb(70, 70, 84), rgb(150, 150, 170), rgb(200, 20, 40), rgb(255, 255, 255), rgb(20, 20, 24), rgb(250, 250, 250) });
    }
    s_palReady = true;
}

const uint16_t* sprite_palette(int id, int theme) {
    if (!s_palReady) build_palettes();
    return s_pal[id][theme];
}

// ------------------------------------------------------------------ blitter
void blit(const Sprite& s, const uint16_t* pal, int x, int y, int dw, int dh, int clipY, bool flip) {
    if (dw <= 0 || dh <= 0 || !s.px) return;
    int x0 = x < 0 ? 0 : x, x1 = x + dw > gfx::W ? gfx::W : x + dw;
    int ymax = clipY < gfx::H ? clipY : gfx::H;
    int y0 = y < 0 ? 0 : y, y1 = y + dh > ymax ? ymax : y + dh;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t stepX = ((uint32_t)s.w << 16) / dw, stepY = ((uint32_t)s.h << 16) / dh;
    uint32_t sx0 = (uint32_t)(x0 - x) * stepX + (stepX >> 1);
    uint32_t sy = (uint32_t)(y0 - y) * stepY + (stepY >> 1);
    for (int j = y0; j < y1; ++j, sy += stepY) {
        const uint8_t* row = s.px + (sy >> 16) * s.w;
        uint16_t* d = gfx::fb + j * gfx::W + x0;
        uint32_t sx = sx0;
        if (!flip) {
            for (int i = x0; i < x1; ++i, sx += stepX, ++d) {
                uint8_t v = row[sx >> 16];
                if (!v) continue;
                *d = v == SHADOW ? gfx::half(*d) : pal[v & 15];
            }
        } else {
            const uint8_t* rowEnd = row + s.w - 1;
            for (int i = x0; i < x1; ++i, sx += stepX, ++d) {
                uint8_t v = rowEnd[-(int)(sx >> 16)];
                if (!v) continue;
                *d = v == SHADOW ? gfx::half(*d) : pal[v & 15];
            }
        }
    }
}
