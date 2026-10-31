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
// 7 tyre 8 tyre light 9 tail 10 tail bright 11 trim 12 plate 13 exhaust 14 stripe 15 specular
enum { C_OUT = 1, C_BD, C_BM, C_BL, C_GD, C_GL, C_TY, C_TYL, C_TAIL, C_TAILB, C_TRIM, C_PLATE, C_EXH, C_STRIPE, C_SPEC };
static const int CAR_W = 112, CAR_H = 58;

// body paint with a soft gradient: lit from the upper left, darker towards the ground and the right flank
static uint8_t body_shade(int x, int y, int y0, int y1) {
    float t = (float)(y - y0) / (float)(y1 - y0);
    float u = (x - 56) / 56.0f;
    float sh = 0.74f - t * 0.72f - u * 0.30f;
    static const float bay[4] = { 0.0f, 0.5f, 0.75f, 0.25f };
    sh += (bay[(x & 1) + 2 * (y & 1)] - 0.375f) * 0.12f;
    return sh > 0.74f ? C_SPEC : sh > 0.46f ? C_BL : sh > 0.10f ? C_BM : C_BD;
}
static void paint(Canvas& c, int y0, int y1) {      // turn the flat C_BM fill into shaded paint
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < CAR_W; ++x)
            if (c.get(x, y) == C_BM) c.set(x, y, body_shade(x, y, y0, y1));
}

static void draw_car_base(Canvas& c) {
    c.ellipse(56, 54, 56, 4.5f, SHADOW);
    // rear tyres: rounded, with tread rings and a bright shoulder
    for (int side = 0; side < 2; ++side) {
        int x0 = side ? 87 : 1;
        float ty[] = { (float)x0 + 3, 31, (float)x0 + 21, 31, (float)x0 + 24, 36, (float)x0 + 24, 50, (float)x0 + 20, 55,
                       (float)x0 + 4, 55, (float)x0, 50, (float)x0, 36 };
        c.poly(ty, 8, C_TY);
        for (int y = 36; y < 54; y += 4) for (int x = x0 + 2; x < x0 + 22; ++x) if (c.get(x, y) == C_TY) c.set(x, y, C_TYL);
        c.rect(x0 + 3, 37, 2, 14, C_TYL);                       // lit shoulder
    }
    // body: sloping shoulders, rounded flares with wheel arches, narrower rear panel between the tyres
    float body[] = { 20, 24, 12, 25, 6, 27.5f, 2.5f, 31, 0.5f, 36, 1, 41,
                     5, 39.5f, 9, 38, 13, 37.5f, 17, 38, 21, 39.5f, 24, 42, 24, 48, 27, 50, 85, 50, 88, 48, 88, 42,
                     91, 39.5f, 95, 38, 99, 37.5f, 103, 38, 107, 39.5f, 111, 41, 111.5f, 36, 109.5f, 31, 106, 27.5f, 100, 25, 92, 24 };
    c.poly(body, 28, C_BM);
    paint(c, 24, 50);
    // cabin: rounded roof, flowing C-pillars
    float cab[] = { 36, 2, 76, 2, 80, 4, 83, 9, 88, 16, 93, 23, 94, 26, 18, 26, 19, 23, 24, 16, 29, 9, 32, 4 };
    c.poly(cab, 12, C_BM);
    paint(c, 2, 26);
    // rear window: sky reflection on top, soft streaks
    float win[] = { 38, 5.5f, 74, 5.5f, 78, 8, 81, 13, 85, 20, 27, 20, 31, 13, 34, 8 };
    c.poly(win, 8, C_GD);
    for (int y = 5; y < 21; ++y)
        for (int x = 24; x < 90; ++x) {
            if (c.get(x, y) != C_GD) continue;
            float t = (y - 5) / 15.0f;
            bool hi = t < 0.30f && ((x + y) & 1) == 0;
            if (hi || (t < 0.14f)) c.set(x, y, C_GL);
        }
    c.quad(42, 22, 53, 6, 58, 6, 47, 22, C_GL);
    c.quad(53, 22, 61, 6, 63, 6, 55, 22, C_GL);
    c.rect(28, 20, 56, 1, C_OUT);                               // lower window seal
    for (int x = 12; x < 100; ++x) if (((x >> 1) & 1) == 0 && c.get(x, 27) != 0) { uint8_t v = c.get(x, 27); if (v == C_BM || v == C_BL || v == C_BD) c.set(x, 27, C_BL); }
    // racing stripes along roof, deck and tail
    for (int y = 2; y < 49; ++y)
        for (int x = 0; x < CAR_W; ++x) {
            bool st = (x >= 49 && x < 54) || (x >= 58 && x < 63);
            uint8_t v = c.get(x, y);
            if (st && (v == C_BM || v == C_BL || v == C_BD || v == C_SPEC)) c.set(x, y, C_STRIPE);
        }
    // spoiler: end plates, pylons, thick blade with a lit top edge
    c.rect(30, 19, 4, 8, C_TRIM); c.rect(78, 19, 4, 8, C_TRIM);
    c.quad(4, 14, 108, 14, 107, 19, 5, 19, C_BD);
    c.rect(4, 14, 104, 1, C_SPEC); c.rect(5, 15, 102, 1, C_BL);
    c.rect(5, 18, 102, 1, C_OUT);
    c.rect(2, 9, 4, 13, C_BM); c.rect(2, 9, 1, 13, C_BL); c.rect(5, 9, 1, 13, C_BD);
    c.rect(106, 9, 4, 13, C_BM); c.rect(106, 9, 1, 13, C_BL); c.rect(109, 9, 1, 13, C_BD);
    // tail lights: lens housings with segments, a slatted centre panel
    for (int x0 : { 8, 78 }) {
        c.rect(x0, 28, 27, 11, C_TRIM);
        c.rect(x0 + 1, 29, 25, 9, C_TAIL);
        c.rect(x0 + 1, 29, 25, 1, C_TAILB);
        c.rect(x0 + 3, 32, 11, 3, C_TAILB);
        for (int k : { 8, 16, 22 }) c.rect(x0 + k, 29, 1, 9, C_OUT);
    }
    c.rect(36, 28, 40, 11, C_TRIM);
    for (int y = 29; y < 38; y += 2) c.rect(37, y, 38, 1, C_OUT);
    c.rect(51, 31, 10, 5, C_STRIPE); c.rect(52, 32, 8, 1, C_SPEC); c.rect(51, 35, 10, 1, C_OUT);
    // number plate with tiny characters
    c.rect(44, 41, 24, 8, C_PLATE);
    c.rect(44, 41, 24, 1, C_OUT); c.rect(44, 48, 24, 1, C_OUT); c.rect(44, 41, 1, 8, C_OUT); c.rect(67, 41, 1, 8, C_OUT);
    for (int k = 0; k < 5; ++k) { c.rect(47 + k * 4, 43, 2, 4, C_OUT); if (k & 1) c.set(48 + k * 4, 45, C_PLATE); }
    // bumper bar, diffuser fins, chrome exhausts
    c.rect(24, 40, 20, 3, C_TRIM); c.rect(68, 40, 20, 3, C_TRIM);
    c.rect(26, 49, 60, 4, C_TRIM);
    for (int x = 28; x < 84; x += 4) c.rect(x, 49, 1, 4, C_OUT);
    for (int ex : { 34, 78 }) {
        c.ellipse(ex, 48.5f, 5.0f, 3.0f, C_EXH);
        c.ellipse(ex, 48.8f, 2.6f, 1.6f, C_OUT);
        c.set(ex - 3, 47, C_PLATE); c.set(ex - 2, 46, C_PLATE);
    }
    // shade under the flares, over the tyres
    for (int side = 0; side < 2; ++side) {
        int x0 = side ? 87 : 1;
        for (int y = 40; y < 43; ++y) for (int x = x0; x < x0 + 24; ++x) { uint8_t v = c.get(x, y); if (v == C_TY || v == C_TYL) c.set(x, y, C_OUT); }
    }
}

#define CAR_USE_MODEL 0
static void make_car_3q(SpriteId id, int dir);
static void render_car(SpriteId id, float yaw, float el, bool straight);

static void make_cars() {
#if CAR_USE_MODEL
    render_car(SPR_CAR, 0.0f, 0.02f, true);
    render_car(SPR_CAR_LEFT, 0.17f, 0.03f, false);
    render_car(SPR_CAR_RIGHT, -0.17f, 0.03f, false);
    for (int i = SPR_CAR; i <= SPR_CAR_RIGHT; ++i) { g_sprites[i].worldW = i == SPR_CAR ? 700.0f : 700.0f * g_sprites[i].w / CAR_W; g_sprites[i].hitW = 0.95f; }
#else
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
        for (int y = 26; y < 39; ++y) {
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
#endif
    make_car_3q(SPR_CAR_LEFT3Q, -1);
    make_car_3q(SPR_CAR_RIGHT3Q, +1);
}


// ---- three-quarter rear views ------------------------------------------------------
// A small software renderer: the car is modelled in 3D (lofted body, cabin, wing, wheels, decals),
// turned by `yaw`, projected with a little perspective and rasterised into the palette-indexed sprite
// with a z-buffer, dithered shading and dark panel outlines. dir < 0: nose to the left (left turn).
namespace {
struct V3 { float x, y, z; };
enum { MT_BODY = 0, MT_GLASS, MT_TYRE, MT_FLAT };
enum { PT_NONE = 0, PT_BODY, PT_CABIN, PT_WING, PT_WHEEL, PT_GLASS, PT_RIM };
struct Tri { V3 p[3]; uint8_t mat, flat, part; float bias; bool smooth; V3 n[3]; };
static Tri s_tri[1400];
static int s_nt;

static void add_tri(V3 a, V3 b, V3 c, uint8_t mat, uint8_t part, uint8_t flat = 0, float bias = 0) {
    if (s_nt >= 1400) return;
    s_tri[s_nt++] = { { a, b, c }, mat, flat, part, bias, false, {} };
}
static void add_quad(V3 a, V3 b, V3 c, V3 d, uint8_t mat, uint8_t part, uint8_t flat = 0, float bias = 0) {
    add_tri(a, b, c, mat, part, flat, bias);
    add_tri(a, c, d, mat, part, flat, bias);
}
static void add_box(float x0, float x1, float y0, float y1, float z0, float z1, uint8_t mat, uint8_t part) {
    V3 v[8] = { {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1} };
    static const int f[6][4] = { {0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0} };
    for (auto& q : f) add_quad(v[q[0]], v[q[1]], v[q[2]], v[q[3]], mat, part);
}
static V3 mix3(V3 a, V3 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }

// wheel: cylinder along x, outer face at xo, thickness th
static void add_wheel(float xo, float th, float cy, float cz, float R) {
    const int N = 14;
    const float xi = xo > 0 ? xo - th : xo + th;
    V3 oo[N], ii[N], hub[N];
    for (int k = 0; k < N; ++k) {
        float a = k * 6.2831853f / N;
        float y = cy + R * sinf(a), z = cz + R * cosf(a);
        oo[k] = { xo, y, z }; ii[k] = { xi, y, z };
        hub[k] = { xo + (xo > 0 ? 0.004f : -0.004f), cy + 0.58f * R * sinf(a), cz + 0.58f * R * cosf(a) };
    }
    for (int k = 0; k < N; ++k) {
        int n = (k + 1) % N;
        add_quad(ii[k], ii[n], oo[n], oo[k], MT_TYRE, PT_WHEEL);
        V3 c0 = { hub[0].x, cy, cz };
        add_tri(c0, hub[k], hub[n], MT_FLAT, PT_RIM, C_EXH, -0.03f);
        add_quad(oo[k], oo[n], hub[n], hub[k], MT_TYRE, PT_WHEEL);
    }
    // centre cap
    V3 c0 = { xo + (xo > 0 ? 0.008f : -0.008f), cy, cz };
    for (int k = 0; k < N; ++k) {
        int n = (k + 1) % N;
        V3 a = { c0.x, cy + 0.2f * R * sinf(k * 6.2831853f / N), cz + 0.2f * R * cosf(k * 6.2831853f / N) };
        V3 b = { c0.x, cy + 0.2f * R * sinf(n * 6.2831853f / N), cz + 0.2f * R * cosf(n * 6.2831853f / N) };
        add_tri(c0, a, b, MT_FLAT, PT_RIM, C_OUT, -0.04f);
    }
}

static void build_car_model() {
    s_nt = 0;
    // ---- body: lofted octagonal sections        z     hw    ybot  ysh   ytop  tw
    float B[6][6] = { { 0.00f, 0.93f, 0.20f, 0.46f, 0.58f, 0.78f },
                                   { 0.45f, 0.96f, 0.17f, 0.49f, 0.60f, 0.80f },
                                   { 2.10f, 0.95f, 0.15f, 0.48f, 0.57f, 0.76f },
                                   { 3.30f, 0.89f, 0.15f, 0.42f, 0.52f, 0.68f },
                                   { 4.00f, 0.82f, 0.17f, 0.32f, 0.40f, 0.60f },
                                   { 4.35f, 0.74f, 0.20f, 0.27f, 0.32f, 0.52f } };
    const float ZS = 0.68f;
    for (int k = 0; k < 6; ++k) B[k][0] *= ZS;
    const int RN = 12;
    V3 ring[6][RN];
    for (int k = 0; k < 6; ++k) {
        float z = B[k][0], hw = B[k][1], yb = B[k][2], ys = B[k][3], yt = B[k][4], tw = B[k][5];
        float sx = tw + (hw - tw) * 0.40f, sy = yt - (yt - ys) * 0.22f;           // rounded shoulder
        V3 L[6] = { { -hw * 0.86f, yb, z }, { -hw, yb + 0.06f, z }, { -hw, ys - 0.05f, z }, { -hw * 0.995f, ys, z }, { -sx, sy, z }, { -tw, yt, z } };
        for (int e = 0; e < 6; ++e) { ring[k][e] = L[e]; ring[k][RN - 1 - e] = { -L[e].x, L[e].y, L[e].z }; }
    }
    auto body_start = s_nt;
    for (int k = 0; k < 5; ++k)
        for (int e = 0; e < RN; ++e) {
            int n = (e + 1) % RN;
            add_quad(ring[k][e], ring[k][n], ring[k + 1][n], ring[k + 1][e], MT_BODY, PT_BODY);
        }
    for (int t = body_start; t < s_nt; ++t) s_tri[t].smooth = true;
    for (int k : { 0, 5 }) {                       // end caps
        V3 c = { 0, 0.40f, B[k][0] };
        for (int e = 0; e < RN; ++e) add_tri(c, ring[k][e], ring[k][(e + 1) % RN], MT_BODY, PT_BODY);
    }
    // ---- cabin: trapezoid sections             z     hwb    hwt   ybase  ytop
    float K[6][5] = { { 0.85f, 0.72f, 0.64f, 0.58f, 0.60f },
                                   { 1.20f, 0.74f, 0.64f, 0.59f, 0.97f },
                                   { 2.05f, 0.74f, 0.62f, 0.58f, 1.00f },
                                   { 2.60f, 0.72f, 0.62f, 0.56f, 0.92f },
                                   { 3.05f, 0.70f, 0.64f, 0.52f, 0.60f },
                                   { 3.20f, 0.66f, 0.62f, 0.50f, 0.51f } };
    for (int k = 0; k < 6; ++k) { K[k][0] *= ZS; }
    V3 cr[6][4];
    for (int k = 0; k < 6; ++k) {
        cr[k][0] = { -K[k][1], K[k][3], K[k][0] }; cr[k][1] = { -K[k][2], K[k][4], K[k][0] };
        cr[k][2] = { K[k][2], K[k][4], K[k][0] };  cr[k][3] = { K[k][1], K[k][3], K[k][0] };
    }
    int cab_start = s_nt;
    for (int k = 0; k < 5; ++k)
        for (int e = 0; e < 3; ++e) {
            bool glassTop = (e == 1 && k == 4);     // rear glass and windscreen
            add_quad(cr[k][e], cr[k][e + 1], cr[k + 1][e + 1], cr[k + 1][e], glassTop ? MT_GLASS : MT_BODY, glassTop ? PT_GLASS : PT_CABIN);
        }
    for (int t = cab_start; t < s_nt; ++t) s_tri[t].smooth = true;
    // pillars around the glass are the body colour: draw the glass inset by shrinking it towards the centre
    // (rear glass inset decal)
    {
        float t0 = 0.14f, t1 = 0.80f;
        V3 a = mix3(cr[0][1], cr[1][1], t0), b = mix3(cr[0][2], cr[1][2], t0), c = mix3(cr[0][2], cr[1][2], t1), d = mix3(cr[0][1], cr[1][1], t1);
        float in = 0.14f;
        a.x += in; d.x += in * 0.8f; b.x -= in; c.x -= in * 0.8f;
        add_quad(a, b, c, d, MT_GLASS, PT_GLASS, 0, -0.012f);
    }
    // side windows (both sides), inset decals along the cabin flank
    for (int sd = -1; sd <= 1; sd += 2) {
        auto side = [&](int k, float t) {          // point on the cabin flank at section k, height fraction t
            float yb = K[k][3], yt = K[k][4];
            float y = yb + (yt - yb) * t;
            float hw = K[k][1] + (K[k][2] - K[k][1]) * t;
            return V3{ sd * (hw + 0.004f), y, K[k][0] };
        };
        V3 q[4][2]; int ks[4] = { 1, 2, 3, 3 };
        (void)ks;
        V3 a0 = side(1, 0.22f), a1 = side(1, 0.84f), b0 = side(2, 0.22f), b1 = side(2, 0.84f), c0 = side(3, 0.22f), c1 = side(3, 0.70f);
        (void)q;
        // shift the window slightly inward in z so pillars show
        a0.z += 0.07f; a1.z += 0.12f; b1.z -= 0.0f; c0.z -= 0.05f; c1.z -= 0.18f;
        add_quad(a0, b0, b1, a1, MT_GLASS, PT_GLASS, 0, -0.012f);
        add_quad(b0, c0, c1, b1, MT_GLASS, PT_GLASS, 0, -0.012f);
    }
    // ---- twin racing stripes on bonnet / roof / deck
    for (int sd = -1; sd <= 1; sd += 2) {
        float xa = sd * 0.05f, xb = sd * 0.17f;
        for (int k = 0; k < 4; ++k) {             // deck + bonnet
            float za = B[k][0], zb = B[k + 1][0], ya = B[k][4] + 0.004f, yb = B[k + 1][4] + 0.004f;
            if (za > 3.1f) continue;
            if (k == 0) { za = 0.0f; }
            if (zb > 3.1f) { float t = (3.1f - za) / (zb - za); zb = 3.1f; yb = ya + (yb - ya) * t; }
            add_quad({ xa, ya, za }, { xb, ya, za }, { xb, yb, zb }, { xa, yb, zb }, MT_FLAT, PT_BODY, C_STRIPE, -0.012f);
        }
        for (int k = 1; k < 3; ++k) {             // roof
            float t = 0.0f;
            (void)t;
            add_quad({ xa, K[k][4] + 0.004f, K[k][0] }, { xb, K[k][4] + 0.004f, K[k][0] },
                     { xb, K[k + 1][4] + 0.004f, K[k + 1][0] }, { xa, K[k + 1][4] + 0.004f, K[k + 1][0] }, MT_FLAT, PT_CABIN, C_STRIPE, -0.012f);
        }
    }
    // ---- rear face details (decals on z = 0)
    const float zr = -0.006f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float x0 = sd * 0.84f, x1 = sd * 0.33f;
        add_quad({ x0, 0.34f, zr }, { x1, 0.34f, zr }, { x1, 0.51f, zr }, { x0, 0.51f, zr }, MT_FLAT, PT_BODY, C_TAIL, -0.01f);
        float m0 = sd * 0.76f, m1 = sd * 0.42f;
        add_quad({ m0, 0.395f, zr - 0.004f }, { m1, 0.395f, zr - 0.004f }, { m1, 0.455f, zr - 0.004f }, { m0, 0.455f, zr - 0.004f }, MT_FLAT, PT_BODY, C_TAILB, -0.012f);
        // exhaust pipes
        for (int k = 0; k < 8; ++k) {
            float a0 = k * 0.7853982f, a1 = (k + 1) * 0.7853982f, cx = sd * 0.55f, cy = 0.215f;
            add_tri({ cx, cy, zr - 0.006f }, { cx + 0.075f * cosf(a0), cy + 0.075f * sinf(a0), zr - 0.006f },
                    { cx + 0.075f * cosf(a1), cy + 0.075f * sinf(a1), zr - 0.006f }, MT_FLAT, PT_BODY, C_EXH, -0.012f);
            add_tri({ cx, cy, zr - 0.008f }, { cx + 0.04f * cosf(a0), cy + 0.04f * sinf(a0), zr - 0.008f },
                    { cx + 0.04f * cosf(a1), cy + 0.04f * sinf(a1), zr - 0.008f }, MT_FLAT, PT_BODY, C_OUT, -0.013f);
        }
    }
    add_quad({ -0.30f, 0.33f, zr }, { 0.30f, 0.33f, zr }, { 0.30f, 0.50f, zr }, { -0.30f, 0.50f, zr }, MT_FLAT, PT_BODY, C_TRIM, -0.01f);
    for (int k = 0; k < 3; ++k)
        add_quad({ -0.28f, 0.37f + k * 0.045f, zr - 0.003f }, { 0.28f, 0.37f + k * 0.045f, zr - 0.003f },
                 { 0.28f, 0.385f + k * 0.045f, zr - 0.003f }, { -0.28f, 0.385f + k * 0.045f, zr - 0.003f }, MT_FLAT, PT_BODY, C_OUT, -0.012f);
    add_quad({ -0.20f, 0.20f, zr }, { 0.20f, 0.20f, zr }, { 0.20f, 0.31f, zr }, { -0.20f, 0.31f, zr }, MT_FLAT, PT_BODY, C_OUT, -0.01f);
    add_quad({ -0.185f, 0.21f, zr - 0.003f }, { 0.185f, 0.21f, zr - 0.003f }, { 0.185f, 0.30f, zr - 0.003f }, { -0.185f, 0.30f, zr - 0.003f }, MT_FLAT, PT_BODY, C_PLATE, -0.012f);
    for (int k = 0; k < 5; ++k)
        add_quad({ -0.16f + k * 0.07f, 0.23f, zr - 0.006f }, { -0.115f + k * 0.07f, 0.23f, zr - 0.006f },
                 { -0.115f + k * 0.07f, 0.28f, zr - 0.006f }, { -0.16f + k * 0.07f, 0.28f, zr - 0.006f }, MT_FLAT, PT_BODY, C_OUT, -0.013f);
    // ---- wing: plate, end plates, uprights
    add_box(-1.02f, 1.02f, 0.66f, 0.715f, 0.06f, 0.42f, MT_BODY, PT_WING);
    add_box(-1.04f, -1.00f, 0.56f, 0.78f, 0.04f, 0.46f, MT_BODY, PT_WING);
    add_box(1.00f, 1.04f, 0.56f, 0.78f, 0.04f, 0.46f, MT_BODY, PT_WING);
    add_box(-0.52f, -0.44f, 0.58f, 0.66f, 0.18f, 0.28f, MT_BODY, PT_WING);
    add_box(0.44f, 0.52f, 0.58f, 0.66f, 0.18f, 0.28f, MT_BODY, PT_WING);
    // ---- wheels
    for (int sd = -1; sd <= 1; sd += 2) {
        add_wheel(sd * 0.99f, 0.32f, 0.20f, 0.36f, 0.20f);
        add_wheel(sd * 0.90f, 0.28f, 0.185f, 3.55f * ZS, 0.185f);
    }
    // smooth normals: average the (outward) face normals of the lofted shells at each shared vertex
    struct Acc { int x, y, z; V3 n; };
    static Acc acc[1400 * 3];
    int na = 0;
    auto key = [](V3 p, int& kx, int& ky, int& kz) { kx = (int)lroundf(p.x * 500); ky = (int)lroundf(p.y * 500); kz = (int)lroundf(p.z * 500); };
    for (int pass = 0; pass < 2; ++pass)
        for (int t = 0; t < s_nt; ++t) {
            Tri& T = s_tri[t];
            if (!T.smooth) continue;
            V3 u = { T.p[1].x - T.p[0].x, T.p[1].y - T.p[0].y, T.p[1].z - T.p[0].z }, v = { T.p[2].x - T.p[0].x, T.p[2].y - T.p[0].y, T.p[2].z - T.p[0].z };
            V3 n = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
            float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
            if (nl < 1e-9f) continue;
            n = { n.x / nl, n.y / nl, n.z / nl };
            V3 c = { (T.p[0].x + T.p[1].x + T.p[2].x) / 3, (T.p[0].y + T.p[1].y + T.p[2].y) / 3, (T.p[0].z + T.p[1].z + T.p[2].z) / 3 };
            V3 ctr = T.part == PT_CABIN || T.part == PT_GLASS ? V3{ 0, 0.55f, c.z } : V3{ 0, 0.30f, c.z };      // push away from the axis
            if (n.x * (c.x - ctr.x) + n.y * (c.y - ctr.y) < 0) n = { -n.x, -n.y, -n.z };
            for (int k = 0; k < 3; ++k) {
                int kx, ky, kz; key(T.p[k], kx, ky, kz);
                if (pass == 0) {
                    int f = -1;
                    for (int a = 0; a < na; ++a) if (acc[a].x == kx && acc[a].y == ky && acc[a].z == kz) { f = a; break; }
                    if (f < 0) { f = na++; acc[f] = { kx, ky, kz, { 0, 0, 0 } }; }
                    acc[f].n = { acc[f].n.x + n.x, acc[f].n.y + n.y, acc[f].n.z + n.z };
                } else {
                    for (int a = 0; a < na; ++a) if (acc[a].x == kx && acc[a].y == ky && acc[a].z == kz) {
                        float l = sqrtf(acc[a].n.x * acc[a].n.x + acc[a].n.y * acc[a].n.y + acc[a].n.z * acc[a].n.z) + 1e-9f;
                        T.n[k] = { acc[a].n.x / l, acc[a].n.y / l, acc[a].n.z / l };
                        break;
                    }
                }
            }
        }
}

// rasterised result of one view
struct Shot { int w, h; float ax, lightL, lightR; };
} // namespace

static void render_car(SpriteId id, float yaw, float el, bool straight) {
    Sprite& s = g_sprites[id];
    build_car_model();
    const float ca = cosf(yaw), sa = sinf(yaw);
    const float ce = cosf(el), se = sinf(el);
    const float D = 15.0f, F = 50.0f * D;
    struct P2 { float x, y, z; };
    auto proj = [&](V3 p, P2& o, V3* world = nullptr) {
        float x = p.x * ca - p.z * sa, z = p.x * sa + p.z * ca, y = p.y;
        if (world) *world = { x, y, z };
        float zc = z * ce - y * se + D;
        o = { F * x / zc, -F * (y * ce + z * se) / zc, zc };
    };
    float mnx = 1e9f, mxx = -1e9f;
    for (int t = 0; t < s_nt; ++t) for (int k = 0; k < 3; ++k) { P2 o; proj(s_tri[t].p[k], o); mnx = fminf(mnx, o.x); mxx = fmaxf(mxx, o.x); }
    P2 ground; proj({ 0, 0, 0 }, ground);
    const int CW = straight ? CAR_W : (int)ceilf(mxx - mnx) + 6;
    const float ox = straight ? CW * 0.5f : 3.0f - mnx, oy = 51.0f - ground.y;
    Canvas c(s, CW, CAR_H);
    const int H = CAR_H;
    float* zb = (float*)plat::allocLarge(CW * H * sizeof(float));
    uint8_t* part = (uint8_t*)plat::allocLarge(CW * H);
    uint8_t* col = (uint8_t*)plat::allocLarge(CW * H);
    for (int i = 0; i < CW * H; ++i) { zb[i] = 1e9f; part[i] = 0; col[i] = 0; }
    float Lx = -0.34f, Ly = 0.62f, Lz = -0.55f; { float l = sqrtf(Lx * Lx + Ly * Ly + Lz * Lz); Lx /= l; Ly /= l; Lz /= l; }
    // half vector for the highlight (viewer straight behind the car)
    float Hx = Lx, Hy = Ly, Hz = Lz - 1.0f; { float l = sqrtf(Hx * Hx + Hy * Hy + Hz * Hz); Hx /= l; Hy /= l; Hz /= l; }
    static const float bay[4] = { 0.0f, 0.5f, 0.75f, 0.25f };

    for (int t = 0; t < s_nt; ++t) {
        const Tri& T = s_tri[t];
        P2 q[3]; V3 w[3];
        for (int k = 0; k < 3; ++k) proj(T.p[k], q[k], &w[k]);
        V3 u = { w[1].x - w[0].x, w[1].y - w[0].y, w[1].z - w[0].z }, v = { w[2].x - w[0].x, w[2].y - w[0].y, w[2].z - w[0].z };
        V3 fn = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        float nl = sqrtf(fn.x * fn.x + fn.y * fn.y + fn.z * fn.z);
        if (nl < 1e-9f) continue;
        fn = { fn.x / nl, fn.y / nl, fn.z / nl };
        V3 cen = { (w[0].x + w[1].x + w[2].x) / 3, (w[0].y + w[1].y + w[2].y) / 3, (w[0].z + w[1].z + w[2].z) / 3 };
        V3 toCam = { -cen.x, 1.6f - cen.y, -D - cen.z };
        if (fn.x * toCam.x + fn.y * toCam.y + fn.z * toCam.z < 0) fn = { -fn.x, -fn.y, -fn.z };
        float bv[3];
        for (int k = 0; k < 3; ++k) {
            V3 n = fn;
            if (T.smooth) {                                  // rotate the model-space vertex normal into the world
                V3 m = T.n[k];
                n = { m.x * ca - m.z * sa, m.y, m.x * sa + m.z * ca };
                if (n.x * fn.x + n.y * fn.y + n.z * fn.z < 0.2f) n = fn;      // safety on creases
            }
            float d = fmaxf(0.0f, n.x * Lx + n.y * Ly + n.z * Lz);
            float sp = fmaxf(0.0f, n.x * Hx + n.y * Hy + n.z * Hz);
            sp = sp * sp; sp = sp * sp; sp = sp * sp; sp = sp * sp;           // ^16
            float grad = (0.62f - T.p[k].y) * 0.42f;                          // darker towards the ground
            bv[k] = 0.14f + 0.74f * d - (grad > 0 ? grad : grad * 0.4f) + (T.mat == MT_FLAT ? 0.0f : 0.38f * sp);
        }
        float x0 = q[0].x + ox, y0 = q[0].y + oy, x1 = q[1].x + ox, y1 = q[1].y + oy, x2 = q[2].x + ox, y2 = q[2].y + oy;
        float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (fabsf(area) < 1e-6f) continue;
        int xmin = (int)floorf(fminf(x0, fminf(x1, x2))), xmax = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
        int ymin = (int)floorf(fminf(y0, fminf(y1, y2))), ymax = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
        if (xmin < 0) xmin = 0;
        if (ymin < 0) ymin = 0;
        if (xmax > CW - 1) xmax = CW - 1;
        if (ymax > H - 1) ymax = H - 1;
        for (int y = ymin; y <= ymax; ++y)
            for (int x = xmin; x <= xmax; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float l0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
                float l1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
                float l2 = 1.0f - l0 - l1;
                const float eps = -0.002f;
                if (l0 < eps || l1 < eps || l2 < eps) continue;
                float z = l0 * q[0].z + l1 * q[1].z + l2 * q[2].z + T.bias;
                int idx = y * CW + x;
                if (z >= zb[idx]) continue;
                zb[idx] = z; part[idx] = T.part;
                float b = l0 * bv[0] + l1 * bv[1] + l2 * bv[2];
                float dv = b + (bay[(x & 1) + 2 * (y & 1)] - 0.375f) * 0.14f;
                uint8_t o;
                switch (T.mat) {
                case MT_BODY:  o = dv > 0.80f ? C_SPEC : dv > 0.52f ? C_BL : dv > 0.22f ? C_BM : C_BD; break;
                case MT_GLASS: o = (dv > 0.95f && ((x + y) & 1) == 0) || dv > 1.3f ? C_GL : C_GD; break;
                case MT_TYRE:  o = dv > 0.60f ? C_TYL : C_TY; break;
                default:       o = T.flat; break;
                }
                col[idx] = o;
            }
    }
    c.ellipse(CW * 0.5f + (straight ? 0 : 4), 53.5f, CW * 0.46f, 4.2f, SHADOW);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < CW; ++x) {
            int i = y * CW + x;
            if (!part[i]) continue;
            bool edge = false;
            const int nx[4] = { -1, 1, 0, 0 }, ny[4] = { 0, 0, -1, 1 };
            for (int k = 0; k < 4 && !edge; ++k) {
                int xx = x + nx[k], yy = y + ny[k];
                if (xx < 0 || yy < 0 || xx >= CW || yy >= H || !part[yy * CW + xx]) { edge = true; break; }
                int j = yy * CW + xx;
                if (part[j] != part[i] && zb[j] > zb[i] + 0.10f) edge = true;
            }
            c.set(x, y, edge ? C_OUT : col[i]);
        }
    plat::freeLarge(zb); plat::freeLarge(part); plat::freeLarge(col);

    P2 rc, tl, tr; proj({ 0, 0.40f, 0 }, rc); proj({ -0.57f, 0.42f, 0 }, tl); proj({ 0.57f, 0.42f, 0 }, tr);
    s.worldW = 700.0f * CW / CAR_W;
    s.hitW = 0.95f;
    s.emissive = (1 << C_TAIL) | (1 << C_TAILB);
    s.anchorX = straight ? 0 : (int16_t)lroundf(rc.x + ox);
    s.lightL = straight ? 0 : (int16_t)lroundf(fminf(tl.x, tr.x) + ox);
    s.lightR = straight ? 0 : (int16_t)lroundf(fmaxf(tl.x, tr.x) + ox);
    if (!straight && yaw > -0.3f && yaw < 0.3f) { /* mild turn: same placement rules as the 3/4 views */ }
}

static void make_car_3q(SpriteId id, int dir) { render_car(id, (dir < 0 ? 1.0f : -1.0f) * 0.40f, 0.07f, false); }

void car_palette(uint16_t out[16], const Livery& l, bool braking) {
    out[0] = 0;
    out[C_OUT] = rgb(16, 12, 20);
    out[C_BD] = gfx::scale(l.body, 150);
    out[C_BM] = l.body;
    out[C_BL] = gfx::lerp(l.body, 0xFFFF, 90);
    out[C_SPEC] = gfx::lerp(l.body, 0xFFFF, 200);
    out[C_GD] = rgb(22, 30, 52);
    out[C_GL] = rgb(118, 150, 186);
    out[C_TY] = rgb(26, 24, 28);
    out[C_TYL] = rgb(58, 56, 62);
    out[C_TAIL] = braking ? rgb(255, 40, 40) : rgb(150, 18, 26);
    out[C_TAILB] = braking ? rgb(255, 230, 200) : rgb(220, 60, 60);
    out[C_TRIM] = rgb(44, 44, 52);
    out[C_PLATE] = rgb(236, 232, 210);
    out[C_EXH] = rgb(150, 150, 160);
    out[C_STRIPE] = l.stripe;
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
    Canvas c(s, 80, 164);
    Rng r(2024);
    c.ellipse(40, 160, 27, 3.5f, SHADOW);
    // trunk with bark ridges
    c.rect(35, 132, 10, 28, V_TM);
    c.rect(35, 132, 3, 28, V_TL);
    c.rect(42, 132, 3, 28, V_TD);
    for (int y = 134; y < 160; y += 5) c.rect(37 + (y % 3), y, 3, 1, V_TD);
    // tiers from the top down: each skirt droops in pointed boughs and shades the one below
    const int tiers = 6;
    for (int i = 0; i < tiers; ++i) {
        float apex = 2 + i * 21.0f;
        float base = apex + 30 + i * 2.0f;
        float hw = 11 + i * 5.2f;
        for (int y = (int)apex; y <= (int)base; ++y) {
            float t = (y - apex) / (base - apex);
            float half = hw * (0.10f + 0.90f * t);
            for (int x = (int)(40 - half); x <= (int)(40 + half); ++x) {
                float u = (x - (40 - half)) / (2 * half + 0.001f);
                bool dith = u > 0.44f && u < 0.60f && ((x + y) & 1);
                c.set(x, y, (u < 0.52f) != dith ? V_LM : V_LD);
            }
        }
        // boughs: drooping strokes from the centre line out to the edge
        int strokes = 4 + i;
        for (int k = 0; k < strokes; ++k) {
            float y0 = apex + 6 + (base - apex - 8) * (k + 0.5f) / strokes;
            float t = (y0 - apex) / (base - apex);
            float half = hw * (0.10f + 0.90f * t);
            for (int sd = -1; sd <= 1; sd += 2) {
                float ex = 40 + sd * half * (0.92f + 0.1f * r.f()), ey = y0 + 5 + 3 * r.f();
                uint8_t col = sd < 0 ? V_LD : V_OUT;
                c.thick(40 + sd * 2.0f, y0 - 2, ex, ey, 0.55f, col);
                if (sd < 0 && (k & 1)) c.thick(40 - 4.0f, y0 - 5, ex + 2, ey - 3, 0.5f, V_LL);   // lit / snowy bough
            }
        }
        // pointed, drooping tips along the lower edge
        for (float x = 40 - hw; x < 40 + hw - 1; x += 10.0f) {
            float e = fabsf(x + 5.0f - 40) / hw;
            float dep = 5.0f + r.f() * 3.0f - e * 2.0f;
            bool lit = x + 5.0f < 40 + hw * 0.04f;
            c.quad(x, base - 3, x + 10, base - 3, x + 5.0f, base + dep, x + 5.0f, base + dep, lit ? V_LM : V_LD);
        }
        // dark gap under the skirt, visible where the next tier does not cover it
        if (i < tiers - 1)
            for (int x = (int)(40 - hw * 0.85f); x <= (int)(40 + hw * 0.85f); ++x)
                if (!c.get(x, (int)base + 6)) c.set(x, (int)base + 6, V_TD);
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


// ---- sprites of the Palm Bay / Glacier Run / Night Harbor looks -----------------------

// parasol: 1 outline 2 pole 3 pole shade 5 stripe A 6 stripe B 7 canopy rim
static void make_parasol() {
    Sprite& s = g_sprites[SPR_PARASOL];
    Canvas c(s, 96, 120);
    c.ellipse(48, 116, 32, 3.5f, SHADOW);
    c.rect(46, 40, 4, 76, 2);
    c.rect(48, 40, 2, 76, 3);
    for (int y = 6; y <= 48; ++y)
        for (int x = 0; x < 96; ++x) {
            float dx = (x + 0.5f - 48) / 46.0f, dy = (y + 0.5f - 46) / 40.0f;
            if (dx * dx + dy * dy > 1.0f || y > 46) continue;
            float a = atan2f(46 - (y + 0.5f), (x + 0.5f) - 48);   // 0..pi
            int k = (int)(a / (3.14159265f / 8.0f));
            uint8_t col = (k & 1) ? 6 : 5;
            if (y > 41 && col == 5) col = 7;
            c.set(x, y, col);
        }
    c.disc(48, 6, 2.5f, 3);
    c.outline(V_OUT);
    s.worldW = 900; s.hitW = 0.08f;
}

// beach hut / chalet: 1 outline 2 wall dark 3 wall 4 wall light 5 roof dark 6 roof 7 roof light 8 accent
static void make_hut() {
    Sprite& s = g_sprites[SPR_HUT];
    Canvas c(s, 128, 108);
    c.ellipse(64, 104, 58, 4, SHADOW);
    c.rect(14, 56, 100, 46, 3);
    for (int x = 14; x < 114; x += 8) c.rect(x, 56, 1, 46, 2);
    c.rect(14, 56, 4, 46, 4);
    c.rect(110, 56, 4, 46, 2);
    float roof[] = { 2, 60, 64, 8, 126, 60 };
    c.poly(roof, 3, 6);
    for (int y = 8; y < 60; ++y)
        for (int x = 0; x < 128; ++x) {
            uint8_t v = c.get(x, y);
            if (v != 6) continue;
            if (y % 6 == 0) c.set(x, y, 5);
            else if (y % 6 == 2 && x < 64) c.set(x, y, 7);
        }
    for (int x = 4; x < 124; x += 5) c.rect(x, 58, 3, 3 + ((x * 7) % 4), 5);
    c.rect(22, 68, 24, 16, 2);              // serving window
    c.rect(20, 64, 28, 4, 8);               // awning
    c.rect(22, 82, 24, 3, 4);               // counter
    c.rect(56, 70, 22, 32, 2);              // door
    c.rect(58, 72, 18, 30, 3);
    c.rect(72, 88, 2, 2, 8);
    float board[] = { 92, 102, 100, 102, 114, 50, 106, 48 };   // leaning surfboard
    c.poly(board, 4, 8);
    c.thick(103, 100, 110, 54, 0.6f, 4);
    c.outline(V_OUT);
    s.worldW = 1700; s.hitW = 0.85f;
    s.emissive = 1 << 8;
}

// lighthouse: 1 outline 2 white shade 3 white 4 red 5 red dark 6 gallery 7 glass 8 lamp
static void make_lighthouse() {
    Sprite& s = g_sprites[SPR_LIGHTHOUSE];
    Canvas c(s, 56, 230);
    c.ellipse(28, 226, 24, 3, SHADOW);
    for (int y = 58; y < 218; ++y) {
        float t = (y - 58) / 160.0f;
        float hw = 11 + 8 * t;
        bool red = ((y - 58) / 27) & 1;
        for (int x = (int)(28 - hw); x <= (int)(28 + hw); ++x) {
            bool shade = x > 28 + hw * 0.25f;
            c.set(x, y, red ? (shade ? 5 : 4) : (shade ? 2 : 3));
        }
    }
    c.rect(6, 214, 44, 12, 2);
    c.rect(24, 196, 8, 18, 6);
    c.rect(8, 52, 40, 6, 6);
    for (int x = 9; x < 47; x += 6) c.rect(x, 44, 2, 8, 6);
    c.rect(19, 30, 18, 22, 7);
    c.disc(28, 41, 6.5f, 8);
    float cap[] = { 15, 31, 28, 14, 41, 31 };
    c.poly(cap, 3, 4);
    c.rect(26, 8, 4, 8, 6);
    c.outline(V_OUT);
    s.worldW = 1000; s.hitW = 0.3f;
    s.emissive = 1 << 8;
}

// ice formation: faceted chunks of glacier ice
// 1 outline 2 shade 3 mid 4 light 5 snow / glint 6 crevasse
static void ice_chunk(Canvas& c, Rng& r, float cx, float base, float w, float h, float lean) {
    float hw = w / 2;
    float tx = cx + lean + (r.f() - 0.5f) * w * 0.2f, ty = base - h;
    float body[] = { cx - hw, base,
                     cx - hw + lean * 0.2f - 2, base - h * 0.45f,
                     cx - w * 0.28f + lean * 0.5f, base - h * 0.8f,
                     tx, ty,
                     cx + w * 0.30f + lean * 0.6f, base - h * 0.72f,
                     cx + hw - 1 + lean * 0.2f, base - h * 0.35f,
                     cx + hw, base };
    c.poly(body, 7, 3);
    float mx = cx + lean * 0.5f - w * 0.05f, my = base - h * 0.45f, bx = cx - w * 0.08f;
    float lit[] = { cx - hw, base, body[2], body[3], body[4], body[5], tx, ty, mx, my, bx, base };
    c.poly(lit, 6, 4);
    float shd[] = { tx, ty, body[8], body[9], body[10], body[11], cx + hw, base, bx, base, mx, my };
    c.poly(shd, 6, 2);
    // snowy top, ragged lower edge
    for (int y = (int)ty; y < (int)(ty + h * 0.24f); ++y)
        for (int x = (int)(cx - hw - 4); x <= (int)(cx + hw + 4 + fabsf(lean)); ++x) {
            uint8_t v = c.get(x, y);
            if (!v || v == SHADOW) continue;
            {   // only this chunk's own pixels (not the blocks behind it)
                bool in = false;
                for (int k = 0, j = 6; k < 7; j = k++) {
                    float xi = body[2 * k], yi = body[2 * k + 1], xj = body[2 * j], yj = body[2 * j + 1];
                    if (((yi > y + 0.5f) != (yj > y + 0.5f)) && (x + 0.5f < (xj - xi) * (y + 0.5f - yi) / (yj - yi) + xi)) in = !in;
                }
                if (!in) continue;
            }
            float lim = h * 0.14f + 5.0f * fabsf(sinf(x * 0.9f + cx)) + r.f() * 2.0f;
            if (y - ty < lim) c.set(x, y, v == 2 ? 4 : 5);
        }
    // ridge, fine ice veins and crevasses
    c.thick(tx, ty + 6, mx, my, 0.45f, 5);
    for (int k = 0; k < 4; ++k) {
        float y0 = my - h * 0.2f + k * h * 0.17f;
        c.thick(mx + 2, y0, mx + w * 0.34f, y0 + h * 0.07f, 0.4f, 3);
    }
    c.thick(cx - w * 0.16f, base - h * 0.55f, cx - w * 0.22f + (r.f() - 0.5f) * 4, base - h * 0.15f, 0.55f, 6);
    c.thick(cx + w * 0.18f, base - h * 0.5f, cx + w * 0.12f, base - h * 0.2f, 0.5f, 6);
    // snow banked at the foot
    c.ellipse(cx - w * 0.1f, base, w * 0.55f, 3.2f, 5);
    c.ellipse(cx + w * 0.2f, base + 1, w * 0.32f, 2.4f, 4);
}

static void make_ice() {
    Sprite& s = g_sprites[SPR_ICE];
    Canvas c(s, 112, 124);
    Rng r(515);
    c.ellipse(56, 119, 50, 4, SHADOW);
    ice_chunk(c, r, 40, 112, 46, 100, -7);    // tall leaning slab, back left
    ice_chunk(c, r, 80, 112, 46, 74, 5);      // right
    ice_chunk(c, r, 14, 113, 26, 40, -2);     // small left
    ice_chunk(c, r, 58, 116, 42, 44, 3);      // low front block
    c.outline(V_OUT);
    s.worldW = 1500; s.hitW = 0.6f;
}

// snowman: 1 outline 2 snow shade 3 snow 4 snow light 5 carrot 6 scarf 7 coal 8 stick
static void make_snowman() {
    Sprite& s = g_sprites[SPR_SNOWMAN];
    Canvas c(s, 56, 82);
    c.ellipse(28, 78, 22, 3, SHADOW);
    c.thick(15, 44, 3, 32, 1.0f, 8); c.thick(41, 44, 53, 30, 1.0f, 8);
    c.thick(4, 32, 1, 26, 0.7f, 8);  c.thick(52, 30, 55, 25, 0.7f, 8);
    c.disc(28, 62, 17, 2);  c.disc(26, 61, 16, 3);  c.disc(23, 57, 8, 4);
    c.disc(28, 40, 12.5f, 2); c.disc(26, 39, 11.5f, 3); c.disc(23, 36, 6, 4);
    c.disc(28, 22, 9.5f, 2);  c.disc(26.5f, 21, 8.5f, 3); c.disc(24, 18.5f, 4, 4);
    c.rect(17, 29, 22, 5, 6); c.rect(33, 33, 5, 12, 6);
    c.rect(23, 18, 2, 2, 7); c.rect(30, 18, 2, 2, 7);
    float nose[] = { 27, 22, 27, 25, 38, 24 };
    c.poly(nose, 3, 5);
    c.rect(26, 40, 3, 3, 7); c.rect(26, 48, 3, 3, 7); c.rect(26, 56, 3, 3, 7);
    c.rect(19, 12, 18, 3, 7); c.rect(21, 3, 14, 10, 7); c.rect(21, 9, 14, 2, 6);
    c.outline(V_OUT);
    s.worldW = 650; s.hitW = 0.45f;
}

// shipping container stack: 1 outline, pairs (light, dark): 3/4 red 5/6 blue 7/8 amber 9/10 green
static void make_container(SpriteId id, uint32_t seed, int rows) {
    Sprite& s = g_sprites[id];
    const int W = 128, H = rows * 32 + 12;
    Canvas c(s, W, H);
    c.ellipse(64, H - 4, 58, 3.5f, SHADOW);
    Rng r(seed);
    for (int row = 0; row < rows; ++row) {
        int y = H - 10 - (row + 1) * 32 + 2;
        int pair = 3 + 2 * (int)(r.f() * 3.99f);
        int jitter = (int)r.range(-6, 6);
        int x0 = 6 + jitter + 6, w = 104;
        c.rect(x0, y, w, 30, pair);
        for (int x = x0 + 2; x < x0 + w - 12; x += 4) c.rect(x, y + 3, 1, 25, pair + 1);
        c.rect(x0, y, w, 2, pair + 1);
        c.rect(x0, y + 28, w, 2, pair + 1);
        c.rect(x0 + w - 12, y, 12, 30, pair + 1);                // doors
        c.rect(x0 + w - 7, y + 3, 1, 24, pair); c.rect(x0 + w - 4, y + 3, 1, 24, pair);
        c.rect(x0 + w - 12, y + 14, 12, 2, 1);
    }
    c.outline(V_OUT);
    s.worldW = 2200; s.hitW = 0.9f;
}

// quay crane: 1 outline 2 steel 3 orange 4 orange dark 5 cabin light 6 red light 7 cable
static void make_crane() {
    Sprite& s = g_sprites[SPR_CRANE];
    Canvas c(s, 200, 270);
    c.ellipse(100, 266, 72, 4, SHADOW);
    for (int lx : { 66, 134 }) {                 // portal legs
        c.rect(lx - 4, 112, 8, 152, 3);
        c.rect(lx + 1, 112, 3, 152, 4);
        c.rect(lx - 7, 258, 14, 6, 2);
    }
    c.rect(62, 200, 76, 6, 4); c.rect(62, 150, 76, 6, 4);
    c.thick(66, 206, 134, 156, 1.1f, 2); c.thick(134, 206, 66, 156, 1.1f, 2);
    c.thick(66, 156, 134, 112, 1.0f, 2);   c.thick(134, 156, 66, 112, 1.0f, 2);
    c.rect(56, 100, 88, 12, 3); c.rect(56, 108, 88, 4, 4);                      // machinery house deck
    // boom: truss reaching over the road side (left), slightly raised
    float boom[] = { 2, 92, 150, 100, 150, 112, 2, 98 };
    c.poly(boom, 4, 3);
    c.thick(2, 98, 150, 112, 0.9f, 4);
    for (int x = 8; x < 150; x += 12) c.thick((float)x, 98 + (x * 0.093f), (float)x + 6, 92 + (x * 0.05f) + 3, 0.6f, 4);
    c.rect(144, 96, 46, 14, 3); c.rect(144, 106, 46, 4, 4);                     // back boom
    float frame[] = { 104, 34, 60, 100, 148, 100 };                            // A frame
    c.poly(frame, 3, 3);
    float hole[] = { 104, 52, 84, 96, 124, 96 };
    c.poly(hole, 3, 0);
    c.thick(104, 34, 4, 94, 0.5f, 7); c.thick(104, 34, 184, 104, 0.5f, 7);
    c.rect(88, 118, 28, 16, 2); c.rect(92, 121, 20, 8, 5);                    // cabin
    c.rect(34, 100, 2, 34, 7); c.rect(30, 132, 10, 8, 2);                      // spreader on cable
    c.disc(4, 90, 2.6f, 6); c.disc(104, 31, 2.6f, 6);
    c.outline(V_OUT);
    s.worldW = 3000; s.hitW = 0.08f;
    s.emissive = (1 << 5) | (1 << 6);
}

// warehouse: 1 outline 2 wall dark 3 wall 4 wall light 5 roof 6 door light 7 window 8 lamp
static void make_warehouse() {
    Sprite& s = g_sprites[SPR_WAREHOUSE];
    Canvas c(s, 200, 100);
    c.ellipse(100, 97, 92, 3.5f, SHADOW);
    c.rect(6, 32, 188, 62, 3);
    for (int x = 8; x < 192; x += 5) c.rect(x, 34, 1, 58, 2);
    c.rect(6, 32, 5, 62, 4);
    float roof[] = { 0, 34, 100, 8, 200, 34 };
    c.poly(roof, 3, 5);
    for (int y = 10; y < 34; y += 4) c.rect(40 + (34 - y), y, 120 - (34 - y) * 2, 1, 2);
    c.rect(70, 52, 60, 42, 2);
    for (int y = 55; y < 92; y += 4) c.rect(73, y, 54, 2, 6);
    for (int x : { 16, 34, 52, 138, 156, 174 }) c.rect(x, 42, 12, 7, 7);
    c.disc(100, 44, 3.2f, 8);
    c.rect(2, 92, 196, 6, 4);
    c.outline(V_OUT);
    s.worldW = 4200; s.hitW = 0.95f;
    s.emissive = (1 << 6) | (1 << 7) | (1 << 8);
}


// ---- roadside dressing --------------------------------------------------------------
// wooden crates and a barrel: 1 outline 2 wood dark 3 wood 4 wood light 5 barrel 6 barrel dark 7 steel band
static void crate(Canvas& c, int x, int y, int w, int h) {
    c.rect(x, y, w, h, 3);
    c.rect(x, y, w, 2, 4); c.rect(x, y, 2, h, 4);
    c.rect(x + w - 2, y, 2, h, 2); c.rect(x, y + h - 2, w, 2, 2);
    c.rect(x + 3, y + 3, w - 6, 1, 2); c.rect(x + 3, y + h - 4, w - 6, 1, 2);
    c.rect(x + 3, y + 3, 1, h - 6, 2); c.rect(x + w - 4, y + 3, 1, h - 6, 2);
    for (int k = 0; k <= 100; ++k) {
        int px = x + 4 + (w - 9) * k / 100, py = y + 4 + (h - 9) * k / 100;
        c.set(px, py, 2); c.set(px + 1, py, 2);
    }
}
static void make_crates() {
    Sprite& s = g_sprites[SPR_CRATES];
    Canvas c(s, 96, 84);
    c.ellipse(48, 80, 44, 3.5f, SHADOW);
    crate(c, 4, 46, 42, 34);
    crate(c, 48, 50, 44, 30);
    crate(c, 12, 14, 36, 32);
    // barrel on the right crate
    c.rect(62, 24, 22, 27, 5); c.rect(78, 24, 6, 27, 6); c.rect(64, 26, 3, 23, 5);
    c.ellipse(73, 24, 11, 3.5f, 7); c.ellipse(73, 51, 11, 3.5f, 6);
    c.rect(62, 30, 22, 2, 7); c.rect(62, 43, 22, 2, 7);
    c.outline(V_OUT);
    s.worldW = 1300; s.hitW = 0.5f;
}

// jersey barrier with a warning lamp: 1 outline 2 grey dark 3 grey 4 (work) 5 red 6 white 7 amber 8 light grey
static void make_barrier() {
    Sprite& s = g_sprites[SPR_BARRIER];
    Canvas c(s, 128, 44);
    c.ellipse(64, 41, 60, 3, SHADOW);
    c.quad(4, 40, 14, 12, 114, 12, 124, 40, 4);
    for (int y = 12; y < 40; ++y)
        for (int x = 0; x < 128; ++x)
            if (c.get(x, y) == 4) c.set(x, y, ((x + y) / 9) & 1 ? 6 : 5);
    c.rect(14, 12, 100, 2, 8);
    c.rect(4, 38, 120, 2, 2);
    c.rect(9, 6, 2, 6, 2);
    c.disc(10, 5, 3.2f, 7);
    c.rect(116, 6, 2, 6, 2);
    c.disc(117, 5, 3.2f, 7);
    c.outline(V_OUT);
    s.worldW = 1500; s.hitW = 0.5f;
    s.emissive = 1 << 7;
}

// tuft of tall beach grass: 1 outline 2 dark 3 mid 4 light 5 sand 6 sand dark 7 seed head
static void make_dunegrass() {
    Sprite& s = g_sprites[SPR_DUNEGRASS];
    Canvas c(s, 72, 56);
    Rng r(808);
    c.ellipse(36, 52, 31, 4.5f, 6);
    c.ellipse(36, 51, 29, 3.2f, 5);
    for (int k = 0; k < 13; ++k) {
        float x0 = 14.0f + k * 3.7f;
        float lean = (k - 6) * 3.4f + r.range(-4, 4);
        float top = 50.0f - r.range(24, 46);
        float xm = x0 + lean * 0.35f, ym = (50.0f + top) * 0.5f;
        float xt = x0 + lean, yt = top;
        uint8_t col = (uint8_t)(2 + (k % 3));
        c.thick(x0, 50, xm, ym, 1.5f, col);
        c.thick(xm, ym, xt, yt, 1.0f, col);
        if (k % 3 == 1) c.disc(xt, yt, 2.0f, 7);
    }
    c.outline(V_OUT);
    s.worldW = 800; s.hitW = 0;
}

// wooden fence: 1 outline 2 dark 3 mid 4 light 5 cap (snow) 6 grass tuft
static void make_fence() {
    Sprite& s = g_sprites[SPR_FENCE];
    Canvas c(s, 128, 60);
    c.ellipse(64, 57, 60, 3, SHADOW);
    for (int ry : { 22, 38 }) {
        c.rect(0, ry, 128, 5, 3);
        c.rect(0, ry, 128, 1, 4);
        c.rect(0, ry + 4, 128, 1, 2);
        c.rect(0, ry - 2, 128, 2, 5);
    }
    for (int x : { 6, 38, 70, 102 }) {
        c.rect(x, 14, 8, 42, 3);
        c.rect(x, 14, 3, 42, 4);
        c.rect(x + 6, 14, 2, 42, 2);
        c.rect(x - 1, 11, 10, 3, 5);
        c.ellipse(x + 4, 55, 8, 3, 6);
    }
    c.outline(V_OUT);
    s.worldW = 1700; s.hitW = 0.3f;
}

// round hay bales: 1 outline 2 straw dark 3 straw 4 straw light 5 strap
static void hay_disc(Canvas& c, float cx, float cy, float rad) {
    for (int y = (int)(cy - rad) - 1; y <= (int)(cy + rad) + 1; ++y)
        for (int x = (int)(cx - rad) - 1; x <= (int)(cx + rad) + 1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            if (d > rad) continue;
            float a = atan2f(dy, dx);
            uint8_t col = ((int)((d + a * 1.1f + 20.0f) / 2.6f) & 1) ? 3 : 4;
            if (d > rad - 2.2f) col = 2;
            else if (dx + dy < -rad * 0.55f && col == 3) col = 4;
            c.set(x, y, col);
        }
    c.disc(cx, cy, 2.6f, 5);
}
static void make_haybale() {
    Sprite& s = g_sprites[SPR_HAYBALE];
    Canvas c(s, 80, 76);
    c.ellipse(40, 72, 36, 3.5f, SHADOW);
    hay_disc(c, 56, 54, 19);
    hay_disc(c, 28, 46, 25);
    c.outline(V_OUT);
    s.worldW = 1100; s.hitW = 0.7f;
}

// flower patch: 1 outline 2 leaf dark 3 leaf 5 red 6 yellow 7 white 8 violet
static void make_flowers() {
    Sprite& s = g_sprites[SPR_FLOWERS];
    Canvas c(s, 96, 44);
    Rng r(1313);
    c.ellipse(48, 36, 42, 7, 2);
    c.ellipse(48, 34, 40, 6, 3);
    for (int k = 0; k < 34; ++k) {
        float fx = r.range(8, 88), fy = r.range(8, 34);
        float ex = (fx - 48) / 42.0f, ey = (fy - 24) / 17.0f;
        if (ex * ex + ey * ey > 1.0f) continue;
        uint8_t col = (uint8_t)(5 + (k % 4));
        c.rect((int)fx, (int)fy + 3, 1, 5, 2);
        int ix = (int)fx, iy = (int)fy;
        c.set(ix, iy, col); c.set(ix - 1, iy, col); c.set(ix + 1, iy, col); c.set(ix, iy - 1, col); c.set(ix, iy + 1, col);
        c.set(ix, iy, col == 6 ? 5 : 6);
    }
    s.worldW = 1200; s.hitW = 0;
}

// snow marker pole: 1 outline 3 red 4 white 5 snow 6 dark 7 amber
static void make_pole() {
    Sprite& s = g_sprites[SPR_POLE];
    Canvas c(s, 40, 132);
    c.ellipse(20, 128, 13, 3, SHADOW);
    c.ellipse(20, 126, 14, 4.5f, 5);
    for (int y = 12; y < 128; ++y) c.rect(18, y, 5, 1, ((y - 12) / 14) & 1 ? 4 : 3);
    c.rect(16, 4, 9, 8, 6);
    c.disc(20.5f, 8, 2.4f, 7);
    float flag[] = { 23, 16, 36, 20, 23, 26 };
    c.poly(flag, 3, 3);
    c.outline(V_OUT);
    s.worldW = 380; s.hitW = 0.1f;
    s.emissive = 1 << 7;
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
    make_parasol();
    make_hut();
    make_lighthouse();
    make_ice();
    make_snowman();
    make_container(SPR_CONTAINER, 41, 3);
    make_container(SPR_CONTAINER2, 97, 2);
    make_crane();
    make_warehouse();
    make_board(SPR_BOARD_D, "SURF", 3, "BEACH CLUB");
    make_board(SPR_BOARD_E, "SKI", 4, "GLACIER LODGE");
    make_board(SPR_BOARD_F, "PORT", 3, "CARGO TERMINAL");
    make_crates();
    make_barrier();
    make_dunegrass();
    make_fence();
    make_haybale();
    make_flowers();
    make_pole();
}

// ------------------------------------------------------------------ palettes
// [sprite][theme][16]
static const int PAL_THEMES = 6;   // THEME_COUNT in track.h
static uint16_t s_pal[SPR_COUNT][PAL_THEMES][16];
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

    // ---- Palm Bay (3), Glacier Run (4), Night Harbor (5) ----
    for (int t = 3; t < 6; ++t) {
        bool tropic = t == 3, glacier = t == 4, harbor = t == 5;
        uint16_t ld = tropic ? rgb(16, 104, 62) : glacier ? rgb(62, 108, 124) : rgb(24, 60, 56);
        uint16_t lm = tropic ? rgb(40, 172, 84) : glacier ? rgb(150, 192, 208) : rgb(40, 90, 80);
        uint16_t ll = tropic ? rgb(150, 232, 92) : glacier ? rgb(250, 252, 255) : rgb(80, 140, 120);
        uint16_t acc = tropic ? rgb(255, 92, 140) : glacier ? rgb(214, 60, 60) : rgb(240, 200, 90);
        set_pal(SPR_PALM, t, { 0, O, rgb(96, 62, 38), rgb(146, 100, 60), rgb(196, 150, 96), ld, lm, ll, rgb(120, 76, 34) });
        set_pal(SPR_PINE, t, { 0, O, rgb(70, 46, 34), rgb(108, 72, 48), rgb(148, 102, 68), ld, lm, ll, acc });
        set_pal(SPR_TREE, t, { 0, O, rgb(70, 46, 34), rgb(108, 72, 48), rgb(148, 102, 68), ld, lm, ll, acc });
        set_pal(SPR_BUSH, t, { 0, O, 0, 0, 0, ld, lm, ll, acc });
        if (tropic)       set_pal(SPR_ROCK, t, { 0, O, rgb(150, 118, 92), rgb(196, 164, 124), rgb(236, 212, 172), 0, 0, rgb(252, 248, 236) });
        else if (glacier) set_pal(SPR_ROCK, t, { 0, O, rgb(66, 92, 140), rgb(118, 154, 202), rgb(190, 224, 250), 0, 0, rgb(250, 252, 255) });
        else              set_pal(SPR_ROCK, t, { 0, O, rgb(38, 44, 56), rgb(66, 74, 90), rgb(100, 110, 128), 0, 0, rgb(128, 138, 156) });
        // billboards
        uint16_t pole = rgb(90, 90, 100);
        if (harbor) {
            set_pal(SPR_BOARD_A, t, { 0, O, pole, rgb(90, 100, 118), rgb(8, 18, 36), rgb(255, 190, 70), rgb(60, 220, 200), rgb(250, 250, 255) });
            set_pal(SPR_BOARD_B, t, { 0, O, pole, rgb(40, 44, 54), rgb(20, 8, 8), rgb(255, 120, 60), rgb(255, 214, 90), rgb(120, 126, 138) });
            set_pal(SPR_BOARD_C, t, { 0, O, pole, rgb(240, 160, 0), rgb(6, 20, 30), rgb(60, 230, 190), rgb(120, 200, 255), rgb(255, 220, 120) });
            set_pal(SPR_BOARD_F, t, { 0, O, pole, rgb(80, 88, 104), rgb(10, 20, 44), rgb(255, 200, 80), rgb(80, 230, 210), rgb(250, 250, 255) });
        } else {
            set_pal(SPR_BOARD_A, t, { 0, O, pole, rgb(200, 200, 210), rgb(30, 60, 160), rgb(255, 214, 0), rgb(255, 255, 255), rgb(250, 250, 255) });
            set_pal(SPR_BOARD_B, t, { 0, O, pole, rgb(40, 40, 48), rgb(220, 30, 40), rgb(255, 255, 255), rgb(255, 210, 0), rgb(120, 120, 130) });
            set_pal(SPR_BOARD_C, t, { 0, O, pole, rgb(240, 160, 0), rgb(20, 20, 30), rgb(255, 140, 0), rgb(200, 200, 210), rgb(255, 220, 120) });
        }
        set_pal(SPR_BOARD_D, t, { 0, O, pole, rgb(250, 250, 240), rgb(0, 150, 190), rgb(255, 255, 255), rgb(255, 210, 60), rgb(255, 255, 255) });
        set_pal(SPR_BOARD_E, t, { 0, O, pole, rgb(150, 100, 64), rgb(20, 56, 120), rgb(255, 255, 255), rgb(255, 120, 40), rgb(200, 150, 100) });
        set_pal(SPR_CHEVRON, t, { 0, O, rgb(120, 120, 130), harbor ? rgb(255, 170, 40) : rgb(255, 210, 0), rgb(20, 20, 24) });
        set_pal(SPR_LAMP, t, { 0, O, rgb(50, 56, 70), rgb(104, 112, 128), rgb(44, 46, 54), rgb(255, 176, 80) });
        set_pal(SPR_GANTRY, t, { 0, O, rgb(70, 70, 84), rgb(150, 150, 170), rgb(200, 20, 40), rgb(255, 255, 255), rgb(20, 20, 24), rgb(250, 250, 250) });
    }
    set_pal(SPR_BUILDING_A, 5, { 0, O, rgb(34, 40, 54), rgb(56, 66, 86), rgb(82, 96, 120), rgb(20, 26, 38), rgb(255, 190, 90), rgb(130, 220, 255), rgb(255, 40, 40) });
    set_pal(SPR_BUILDING_B, 5, { 0, O, rgb(48, 38, 44), rgb(78, 62, 70), rgb(108, 88, 98), rgb(26, 22, 28), rgb(255, 170, 70), rgb(120, 230, 210), rgb(255, 40, 40) });
    set_pal(SPR_BUILDING_C, 5, { 0, O, rgb(26, 44, 54), rgb(42, 66, 80), rgb(64, 94, 112), rgb(16, 28, 36), rgb(255, 214, 120), rgb(110, 255, 220), rgb(255, 40, 40) });
    for (int t = 3; t < 6; ++t) {
        set_pal(SPR_PARASOL, t, { 0, O, rgb(214, 214, 222), rgb(150, 150, 160), 0, rgb(238, 60, 60), rgb(252, 252, 252), rgb(196, 36, 48) });
        set_pal(SPR_LIGHTHOUSE, t, { 0, O, rgb(196, 204, 216), rgb(248, 248, 252), rgb(214, 40, 48), rgb(146, 24, 34), rgb(62, 66, 80), rgb(150, 222, 255), rgb(255, 244, 170) });
        set_pal(SPR_ICE, t, { 0, rgb(34, 58, 104), rgb(92, 140, 192), rgb(140, 196, 230), rgb(196, 232, 250), rgb(246, 251, 255), rgb(52, 92, 150) });
        set_pal(SPR_SNOWMAN, t, { 0, O, rgb(184, 202, 230), rgb(240, 246, 255), rgb(255, 255, 255), rgb(255, 130, 30), rgb(220, 40, 50), rgb(30, 30, 40), rgb(110, 76, 50) });
        set_pal(SPR_CONTAINER, t, { 0, O, 0, rgb(196, 56, 46), rgb(132, 36, 34), rgb(46, 100, 174), rgb(28, 64, 122), rgb(224, 156, 44), rgb(152, 100, 26), rgb(52, 144, 100), rgb(32, 94, 66) });
        set_pal(SPR_CONTAINER2, t, { 0, O, 0, rgb(196, 56, 46), rgb(132, 36, 34), rgb(46, 100, 174), rgb(28, 64, 122), rgb(224, 156, 44), rgb(152, 100, 26), rgb(52, 144, 100), rgb(32, 94, 66) });
        set_pal(SPR_CRANE, t, { 0, O, rgb(64, 70, 86), rgb(232, 124, 34), rgb(160, 78, 22), rgb(255, 226, 130), rgb(255, 40, 40), rgb(130, 136, 150) });
        set_pal(SPR_WAREHOUSE, t, { 0, O, rgb(46, 54, 70), rgb(76, 88, 108), rgb(110, 124, 146), rgb(38, 42, 54), rgb(255, 196, 96), rgb(140, 212, 255), rgb(255, 236, 170) });
    }
    // tropical: sunny beach hut; glacier: snowy timber chalet
    set_pal(SPR_HUT, 3, { 0, O, rgb(206, 122, 60), rgb(246, 156, 74), rgb(255, 196, 116), rgb(122, 92, 42), rgb(196, 154, 72), rgb(232, 204, 114), rgb(60, 204, 224) });
    set_pal(SPR_HUT, 4, { 0, O, rgb(104, 66, 40), rgb(148, 96, 58), rgb(188, 130, 80), rgb(168, 194, 222), rgb(228, 240, 252), rgb(255, 255, 255), rgb(255, 222, 130) });
    set_pal(SPR_HUT, 5, { 0, O, rgb(60, 66, 80), rgb(90, 98, 116), rgb(122, 132, 152), rgb(40, 44, 56), rgb(70, 76, 90), rgb(100, 108, 126), rgb(255, 190, 90) });
    // roadside dressing, same look in every theme (fence caps and tufts follow the snow)
    for (int t = 0; t < 6; ++t) {
        bool snowy = t == 1 || t == 4;
        set_pal(SPR_CRATES, t, { 0, O, rgb(94, 62, 34), rgb(150, 104, 58), rgb(200, 152, 92), rgb(40, 96, 170), rgb(26, 60, 118), rgb(206, 210, 220) });
        set_pal(SPR_BARRIER, t, { 0, O, rgb(90, 92, 104), rgb(150, 154, 166), 0, rgb(214, 48, 44), rgb(246, 246, 246), rgb(255, 176, 40), rgb(200, 204, 214) });
        set_pal(SPR_DUNEGRASS, t, { 0, O, rgb(52, 120, 56), rgb(104, 170, 66), rgb(190, 214, 100), rgb(240, 216, 150), rgb(214, 186, 120), rgb(224, 186, 110) });
        set_pal(SPR_FENCE, t, { 0, O, rgb(92, 64, 40), rgb(142, 100, 62), rgb(190, 146, 96), snowy ? rgb(246, 250, 255) : rgb(214, 176, 126),
                                 t == 4 ? rgb(214, 228, 246) : t == 1 ? rgb(236, 244, 252) : rgb(84, 156, 58) });
        set_pal(SPR_HAYBALE, t, { 0, O, rgb(176, 128, 40), rgb(226, 180, 64), rgb(250, 222, 120), rgb(120, 84, 34) });
        set_pal(SPR_FLOWERS, t, { 0, O, rgb(30, 96, 44), rgb(60, 150, 60), 0, rgb(236, 52, 70), rgb(255, 214, 40), rgb(255, 255, 255), rgb(160, 90, 220) });
        set_pal(SPR_POLE, t, { 0, O, 0, rgb(222, 40, 44), rgb(248, 248, 250), rgb(236, 244, 255), rgb(30, 34, 48), rgb(255, 176, 40) });
    }
    // alpine chalet (timber with a snowy roof), like the glacier one
    set_pal(SPR_HUT, 1, { 0, O, rgb(104, 66, 40), rgb(148, 96, 58), rgb(188, 130, 80), rgb(168, 194, 222), rgb(228, 240, 252), rgb(255, 255, 255), rgb(255, 222, 130) });
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
