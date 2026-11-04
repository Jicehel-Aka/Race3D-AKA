// render.cpp - the "OutRun style" segment renderer and everything on top.
#include "render.h"
#include "i18n.h"
#include "car_art.h"
#include "gfx.h"
#include "platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace gfx;

namespace {

constexpr int DRAW_DIST = 170;
constexpr int HORIZON = H / 2;

struct PSeg {
    float x1, y1, w1, s1;   // near edge: screen x, y, half road width, scale
    float x2, y2, w2, s2;   // far edge
    int clip;               // rows >= clip are hidden by nearer road (for sprites)
    int fog;                // 0..256
    int pit;                // pitIdx of the segment (negative on the last segments of the lap)
    int idx;
    bool valid;
};
PSeg s_ps[DRAW_DIST];

uint16_t s_sky[H];
uint16_t s_farGrad[128], s_nearGrad[128];
int s_cacheTheme = -1;

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

void build_theme_cache(int theme) {
    const Theme& th = g_themes[theme];
    for (int y = 0; y < H; ++y) {
        float t = (float)y / HORIZON;
        if (t > 1) t = 1;
        s_sky[y] = t < 0.6f ? lerp(th.skyTop, th.skyMid, (int)(t / 0.6f * 256))
                            : lerp(th.skyMid, th.skyHorizon, (int)((t - 0.6f) / 0.4f * 256));
    }
    for (int a = 0; a < 128; ++a) {
        int k = a * 256 / 90; if (k > 256) k = 256;
        // far layer: hazy at the base, crisper at the top
        uint16_t base = lerp(th.farCol, th.fog, 150);
        s_farGrad[a] = lerp(base, th.farCol, k);
        int kn = a * 256 / 50; if (kn > 256) kn = 256;
        s_nearGrad[a] = lerp(lerp(th.nearCol, th.fog, 60), th.nearCol, kn);
    }
    s_cacheTheme = theme;
}

// ------------------------------------------------------------------ sky
// The sky (gradient, sun / moon, clouds, stars) is painted once per theme into
// a 1024 pixel wide panorama in PSRAM, then just scrolled every frame.
constexpr int PANO_W = 1024, PANO_H = HORIZON + 8;
uint16_t* s_pano = nullptr;
int s_panoTheme = -1;

// paints panorama columns [c0, c0 + W) into rows [0, PANO_H) of fb
void paint_sky_chunk(int theme, int c0) {
    const Theme& th = g_themes[theme];
    for (int y = 0; y < PANO_H; ++y) hline(0, W, y, s_sky[y]);
    // call fn(x) for every on-screen copy of panorama column px (wrap around)
    auto at = [&](int px, int margin, auto fn) {
        for (int k = -1; k <= 1; ++k) {
            int x = px - c0 + k * PANO_W;
            if (x > -margin && x < W + margin) fn(x);
        }
    };
    if (theme == THEME_COAST) {
        at(600, 120, [&](int cx) {   // retro sun with slices
            int cy = HORIZON - 24, rad = 36;
            blendCircle(cx, cy, rad + 26, rgb(255, 150, 120), 40);
            blendCircle(cx, cy, rad + 12, rgb(255, 170, 120), 70);
            for (int dy = -rad; dy <= rad; ++dy) {
                if (dy > 4) {
                    int band = dy - 4, gap = 1 + band / 7;
                    if ((band % 7) < gap) continue;
                }
                int dx = (int)sqrtf((float)(rad * rad - dy * dy));
                hline(cx - dx, cx + dx + 1, cy + dy, lerp(rgb(255, 246, 140), rgb(255, 70, 130), (dy + rad) * 256 / (2 * rad)));
            }
        });
        for (int k = 0; k < 10; ++k) {   // streaky clouds
            uint32_t hsh = hash32(k * 977 + 13);
            int y = 22 + (int)(hsh % 64);
            int len = 40 + (int)((hsh >> 8) % 100);
            at((int)((hsh >> 16) % PANO_W), 160, [&](int x) {
                blendRect(x, y, len, 2, rgb(255, 170, 190), 120);
                blendRect(x + 10, y + 3, len - 20, 1, rgb(255, 140, 170), 90);
                blendRect(x - 8, y - 2, len / 2, 1, rgb(255, 200, 210), 60);
            });
        }
    } else if (theme == THEME_ALPINE) {
        at(760, 60, [&](int cx) {
            glowCircle(cx, 34, 40, rgb(90, 80, 40));
            fillCircle(cx, 34, 11, rgb(255, 252, 230));
        });
        for (int k = 0; k < 12; ++k) {   // puffy clouds
            uint32_t hsh = hash32(k * 7919 + 5);
            int y = 16 + (int)((hsh >> 12) % 62);
            int s = 9 + (int)((hsh >> 20) % 13);
            at((int)(hsh % PANO_W), 60, [&](int x) {
                blendCircle(x, y + 4, s, rgb(170, 200, 230), 120);
                blendCircle(x - s, y + 5, s * 2 / 3, rgb(255, 255, 255), 170);
                blendCircle(x + s, y + 4, s * 3 / 4, rgb(255, 255, 255), 170);
                blendCircle(x, y, s, rgb(255, 255, 255), 210);
            });
        }
    } else if (theme == THEME_TROPIC) {
        at(480, 90, [&](int cx) {            // high tropical sun
            glowCircle(cx, 30, 46, rgb(70, 66, 28));
            fillCircle(cx, 30, 12, rgb(255, 255, 232));
        });
        for (int k = 0; k < 9; ++k) {        // small fair-weather cumulus
            uint32_t hsh = hash32(k * 4421 + 9);
            int y = 22 + (int)((hsh >> 12) % 56);
            int s = 6 + (int)((hsh >> 20) % 7);
            at((int)(hsh % PANO_W), 50, [&](int x) {
                blendCircle(x, y + 3, s, rgb(190, 225, 240), 110);
                blendCircle(x - s, y + 4, s * 2 / 3, rgb(255, 255, 255), 190);
                blendCircle(x + s, y + 3, s * 2 / 3, rgb(255, 255, 255), 190);
                blendCircle(x, y, s, rgb(255, 255, 255), 230);
            });
        }
        for (int k = 0; k < 6; ++k) {        // thin high cirrus
            uint32_t hsh = hash32(k * 613 + 41);
            int y = 10 + (int)((hsh >> 8) % 40), len = 50 + (int)((hsh >> 16) % 80);
            at((int)(hsh % PANO_W), 140, [&](int x) {
                blendRect(x, y, len, 1, rgb(255, 255, 255), 90);
                blendRect(x + 12, y + 2, len - 24, 1, rgb(255, 255, 255), 50);
            });
        }
    } else if (theme == THEME_GLACIER) {
        for (int k = 0; k < 110; ++k) {      // stars
            uint32_t hsh = hash32(k * 2237 + 3);
            int y = (int)((hsh >> 10) % (HORIZON - 40));
            uint16_t c = ((hsh >> 24) & 3) ? rgb(210, 220, 250) : rgb(250, 240, 220);
            at((int)(hsh % PANO_W), 2, [&](int x) { pset(x, y, c); });
        }
        for (int x = 0; x < W; ++x) {        // aurora curtains (seamless over the 1024 px panorama)
            float ph = (float)((c0 + x) % PANO_W) / PANO_W * 6.2831853f;
            float yc = 36.0f + 12.0f * sinf(ph * 2) + 5.0f * sinf(ph * 5 + 1.0f);
            float kk = 0.5f + 0.5f * sinf(ph * 3 + 0.5f);
            uint16_t col = lerp(rgb(70, 255, 160), rgb(150, 90, 255), (int)(kk * 180));
            float amp = 0.55f + 0.45f * sinf(ph * 4 + 2.0f);
            for (int dy = -4; dy < 34; ++dy) {
                int a = dy < 0 ? (4 + dy) * 22 : (int)((1.0f - dy / 34.0f) * 130);
                a = (int)(a * amp);
                if (a > 0) blendRect(x, (int)yc + dy, 1, 1, col, a);
            }
        }
        at(700, 90, [&](int cx) {            // low polar sun
            glowCircle(cx, HORIZON - 8, 48, rgb(104, 64, 64));
            fillCircle(cx, HORIZON - 4, 10, rgb(255, 228, 196));
        });
    } else if (theme == THEME_HARBOR) {
        for (int k = 0; k < 70; ++k) {       // stars
            uint32_t hsh = hash32(k * 977 + 21);
            int y = (int)((hsh >> 10) % (HORIZON - 50));
            uint16_t c = rgb(170, 180, 210);
            at((int)(hsh % PANO_W), 2, [&](int x) { pset(x, y, c); });
        }
        for (int k = 0; k < 34; ++k) blendRect(0, HORIZON - 1 - k, W, 1, rgb(255, 150, 60), (34 - k) * 2);   // sodium glow of the port
        at(300, 80, [&](int mx) {            // full moon
            glowCircle(mx, 44, 42, rgb(34, 40, 64));
            fillCircle(mx, 44, 15, rgb(240, 244, 230));
            fillCircle(mx - 5, 41, 3, rgb(212, 220, 208));
            fillCircle(mx + 5, 49, 2, rgb(212, 220, 208));
            fillCircle(mx + 4, 38, 2, rgb(220, 228, 216));
        });
        for (int k = 0; k < 8; ++k) {        // dark clouds, moon-lit edges
            uint32_t hsh = hash32(k * 1291 + 5);
            int y = 18 + (int)((hsh >> 12) % 60), len = 50 + (int)((hsh >> 20) % 90);
            at((int)(hsh % PANO_W), 150, [&](int x) {
                blendRect(x, y, len, 3, rgb(30, 44, 74), 150);
                blendRect(x + 6, y, len / 2, 1, rgb(120, 140, 172), 70);
            });
        }
        for (int b = 0; b < 2; ++b)          // port searchlights
            at(b ? 820 : 130, 200, [&](int bx) {
                int dir = b ? -1 : 1;
                for (int dy = 0; dy < HORIZON - 8; ++dy) {
                    int cx = bx + dir * dy * 7 / 10, w = 3 + dy / 6;
                    int a = 30 - dy / 6; if (a < 8) a = 8;
                    blendRect(cx - w / 2, HORIZON - 2 - dy, w, 1, rgb(200, 224, 245), a);
                }
            });
    } else {
        for (int k = 0; k < 160; ++k) {  // stars
            uint32_t hsh = hash32(k * 31337 + 7);
            int y = (int)((hsh >> 10) % (HORIZON - 26));
            uint16_t c = ((hsh >> 24) & 3) ? rgb(200, 200, 240) : rgb(240, 210, 160);
            at((int)(hsh % PANO_W), 2, [&](int x) {
                pset(x, y, c);
                if (((hsh >> 20) & 7) == 0) { pset(x + 1, y, c); pset(x - 1, y, c); pset(x, y + 1, c); pset(x, y - 1, c); }
            });
        }
        at(300, 60, [&](int mx) {       // crescent moon
            glowCircle(mx, 40, 34, rgb(40, 36, 70));
            fillCircle(mx, 40, 14, rgb(250, 246, 220));
            fillCircle(mx + 7, 36, 12, s_sky[36]);
        });
    }
    (void)th;
}

void build_pano(int theme) {
    if (!s_pano) s_pano = (uint16_t*)plat::allocLarge(PANO_W * PANO_H * sizeof(uint16_t));
    uint16_t* tmp = (uint16_t*)plat::allocLarge(W * H * sizeof(uint16_t));
    if (!s_pano || !tmp) { if (tmp) plat::freeLarge(tmp); return; }
    uint16_t* saved = fb;
    fb = tmp;
    for (int c0 = 0; c0 < PANO_W; c0 += W) {
        paint_sky_chunk(theme, c0);
        int n = PANO_W - c0 < W ? PANO_W - c0 : W;
        for (int y = 0; y < PANO_H; ++y) memcpy(s_pano + y * PANO_W + c0, tmp + y * W, n * sizeof(uint16_t));
    }
    fb = saved;
    plat::freeLarge(tmp);
    s_panoTheme = theme;
}

void draw_sky(const Race& r, float time) {
    int theme = r.track.theme;
    if (s_panoTheme != theme) build_pano(theme);
    int sh = ((int)(r.skyOffset * 0.5f)) & (PANO_W - 1);
    if (!s_pano) {
        for (int y = 0; y < PANO_H; ++y) hline(0, W, y, s_sky[y]);
        return;
    }
    int n1 = PANO_W - sh < W ? PANO_W - sh : W;
    for (int y = 0; y < PANO_H; ++y) {
        const uint16_t* row = s_pano + y * PANO_W;
        uint16_t* dst = fb + y * W;
        memcpy(dst, row + sh, n1 * sizeof(uint16_t));
        if (n1 < W) memcpy(dst + n1, row, (W - n1) * sizeof(uint16_t));
    }
    if (theme == THEME_CITY || theme == THEME_HARBOR) {          // a few twinkling stars
        for (int k = 0; k < 16; ++k) {
            uint32_t hsh = hash32(k * 31337 + 7);
            int x = ((int)(hsh % PANO_W) - sh) & (PANO_W - 1);
            if (x >= W) continue;
            int y = (int)((hsh >> 10) % (HORIZON - 26));
            int tw = (int)(sinf(time * 4 + k * 1.7f) * 100 + 156);
            pset(x, y, scale(rgb(255, 255, 255), tw));
        }
    }
}

// Glacier Run backdrop: broad, softly lit massifs. Everything is a smooth function of the column
// (no per-column hashes) so there are no vertical seams: light follows the wide slope, the snow line
// wanders and fades into rock, ice blooms in soft patches, and the foot of the range melts into the haze.
void glacier_far(const Track& tr, const Theme& th, int off, int baseY) {
    const int M = Track::BG_W - 1;
    const uint16_t snowLit = rgb(250, 252, 255), snowSh = rgb(196, 210, 238);
    const uint16_t rockLit = rgb(124, 130, 160), rockSh = rgb(88, 96, 136);
    const uint16_t iceLit = rgb(176, 216, 240), iceSh = rgb(138, 176, 218);
    for (int x = 0; x < W; ++x) {
        int col = (x + off) & M;
        uint16_t* p = fb + x;
        // pale back range, slower parallax and wider
        int bcol = ((((x + off / 2) >> 1)) + 411) & M;
        int hb = 14 + tr.farH[bcol] * 6 / 10;
        for (int y = baseY - hb < 0 ? 0 : baseY - hb; y < baseY && y < H; ++y) {
            int alt = baseY - y, d = hb - alt;
            uint16_t c = lerp(th.fog, rgb(214, 226, 248), 150);
            c = lerp(c, rgb(246, 250, 255), d < 10 ? 160 - d * 10 : 60);
            p[y * W] = c;
        }
        int h = tr.farH[col];
        // light: slope averaged over a wide window, mapped smoothly to 0..255 (0 = fully shaded)
        int sl = ((int)tr.farH[(col + 28) & M] + tr.farH[(col + 18) & M] + tr.farH[(col + 8) & M]) - ((int)tr.farH[(col - 8) & M] + tr.farH[(col - 18) & M] + tr.farH[(col - 28) & M]);
        int lit = 150 - sl * 4;
        lit = lit < 20 ? 20 : lit > 255 ? 255 : lit;
        float fc = (float)col;
        int snowLine = 30 + (int)(7.0f * sinf(fc * 0.021f) + 4.0f * sinf(fc * 0.057f + 1.3f));
        int iceAmt = (int)(128.0f + 127.0f * sinf(fc * 0.017f + 2.0f) * sinf(fc * 0.043f)) ;   // soft patches
        iceAmt = iceAmt < 150 ? 0 : (iceAmt - 150) * 2;
        if (iceAmt > 190) iceAmt = 190;
        for (int y = baseY - h < 0 ? 0 : baseY - h; y < baseY && y < H; ++y) {
            int alt = baseY - y, d = h - alt;
            int sm = (alt - snowLine) * 22 + 128;          // soft snow/rock transition
            sm = sm < 0 ? 0 : sm > 255 ? 255 : sm;
            uint16_t snow = lerp(snowSh, snowLit, lit);
            snow = lerp(snow, rgb(176, 198, 236), d > 24 ? 70 : d * 3);              // bluer lower down
            uint16_t rock = lerp(rockSh, rockLit, lit);
            uint16_t ic = lerp(iceSh, iceLit, lit);
            rock = lerp(rock, ic, iceAmt);
            uint16_t c = lerp(rock, snow, sm);
            // aerial haze towards the foot of the range
            int haze = alt < 24 ? 205 - alt * 7 : 37;
            if (d < 2) c = lerp(c, snowLit, 90);           // thin bright rim on the crest
            p[y * W] = lerp(c, th.fog, haze);
        }
    }
}

// Sea along the horizon (Palm Bay, Night Harbor): graded water, wave glints, sailing boats / moored ships.
void sea_band(const Track& tr, const Theme& th, int off, int baseY) {
    (void)th;
    const int M = Track::BG_W - 1;
    const bool harbor = tr.theme == THEME_HARBOR;
    const int SH = harbor ? 11 : 12;
    for (int x = 0; x < W; ++x) {
        int col = (x + off) & M;
        uint16_t* p = fb + x;
        for (int k = 0; k < SH; ++k) {
            int y = baseY - SH + k;
            if (y < 0 || y >= H) continue;
            int kk = k * 255 / (SH - 1);
            uint16_t c;
            if (harbor) {
                c = lerp(rgb(30, 50, 82), rgb(8, 18, 36), kk);
                if (k > 0 && (hash32((col / 3) * 13 + 5) % 9) == 0 && (k & 1) == 0) c = lerp(c, rgb(255, 190, 90), 150 - k * 14);   // lamp reflections
                else if ((col * 3 + k * 7) % 23 == 0) c = lerp(c, rgb(120, 150, 200), 90);
            } else {
                c = lerp(rgb(150, 226, 226), rgb(18, 142, 186), kk);
                if (k > 1 && (col * 3 + k * 7) % 17 == 0) c = lerp(c, rgb(255, 255, 255), 110);
                if (hash32(col * 31 + k * 977) % 89 == 0) c = rgb(255, 255, 255);
            }
            p[y * W] = c;
        }
    }
    if (harbor) {
        for (int bc : { 330, 760 }) {                        // moored ships with lit portholes
            int sx = (bc - off) & M;
            if (sx > W) continue;
            int by = baseY - 1;
            for (int x = 0; x < 46; ++x) {
                int px = sx + x; if (px < 0 || px >= W) continue;
                int hullTop = x < 4 ? by - 3 + (4 - x) / 2 : by - 4;
                for (int y = hullTop; y <= by; ++y) if (y >= 0 && y < H) fb[y * W + px] = (y == by) ? rgb(120, 28, 30) : rgb(26, 32, 46);
                if (x > 2 && x < 43 && (x & 3) == 0 && by - 2 >= 0) fb[(by - 2) * W + px] = rgb(255, 210, 110);
                if (x >= 4 && x < 28 && by - 8 >= 0) {          // deck load of containers
                    uint16_t cc = ((x / 6) & 1) ? rgb(196, 56, 46) : rgb(46, 100, 174);
                    for (int y = by - 8; y < by - 4; ++y) fb[y * W + px] = (x % 6 == 0) ? scale(cc, 160) : cc;
                }
                if (x >= 32 && x < 42) for (int y = by - 10; y < by - 4; ++y) if (y >= 0) fb[y * W + px] = (y == by - 8) ? rgb(255, 210, 110) : rgb(184, 190, 204);
                if (x >= 35 && x < 39) for (int y = by - 14; y < by - 10; ++y) if (y >= 0) fb[y * W + px] = (y == by - 12) ? rgb(220, 40, 44) : rgb(70, 74, 90);
            }
        }
    } else {
        for (int bc : { 150, 530, 880 }) {                    // sailing boats
            int sx = (bc - off) & M;
            if (sx > W) continue;
            int by = baseY - 2;
            for (int x = -5; x <= 5; ++x) { int px = sx + x; if (px >= 0 && px < W && by >= 0) fb[by * W + px] = (x == -5 || x == 5) ? rgb(250, 250, 250) : rgb(210, 70, 60); }
            for (int y = 1; y <= 13; ++y) {
                int yy = by - y; if (yy < 0) break;
                int wl = (14 - y) * 5 / 13, wr = (14 - y) * 3 / 13;
                for (int x = -wl; x <= 0; ++x) { int px = sx + x; if (px >= 0 && px < W) fb[yy * W + px] = x > -wl / 3 ? rgb(236, 240, 246) : rgb(255, 255, 255); }
                for (int x = 1; x <= wr; ++x) { int px = sx + x; if (px >= 0 && px < W) fb[yy * W + px] = rgb(214, 222, 236); }
            }
        }
    }
}

void draw_layers(const Race& r, int baseY) {
    const Track& tr = r.track;
    const Theme& th = g_themes[tr.theme];
    const int M = Track::BG_W - 1;
    int off = (int)r.skyOffset, offN = (int)r.nearOffset;
    if (tr.theme == THEME_GLACIER) glacier_far(tr, th, off, baseY);
    for (int x = 0; x < W && tr.theme != THEME_GLACIER; ++x) {
        int col = (x + off) & M;
        int h = tr.farH[col];
        int top = baseY - h;
        uint16_t* p = fb + x;
        for (int y = top < 0 ? 0 : top; y < baseY && y < H; ++y) {
            int alt = baseY - y;
            uint16_t c = s_farGrad[alt > 127 ? 127 : alt];
            if (tr.theme == THEME_ALPINE || tr.theme == THEME_GLACIER) {
                int snowLine = (tr.theme == THEME_GLACIER ? 38 : 58) + (int)(hash32(col) % 6);
                int slope = (int)tr.farH[(col + 3) & M] - (int)tr.farH[(col - 3) & M];   // > 0 : faces away from the sun
                if (alt > snowLine) {
                    if (tr.theme == THEME_GLACIER) c = slope > 1 ? lerp(th.farHi, th.farCol, 70) : th.farHi;
                    else c = (slope > 1 || ((col ^ y) & 7) == 0) ? lerp(th.farHi, th.farCol, 80) : th.farHi;
                }
                else if (slope > 0) c = scale(c, 256 - (slope > 8 ? 8 : slope) * 7);
            } else if (tr.theme == THEME_CITY || tr.theme == THEME_HARBOR) {
                if (alt > 3 && alt < h - 2 && ((col / 2) & 1) == 0 && ((y + 1) % 4) == 0 && (hash32(col * 131 + y) % 5) == 0) c = th.farHi;
            } else if (alt > h - 3) {
                c = lerp(c, th.farHi, 160);
            }
            p[y * W] = c;
        }
    }
    if (tr.theme == THEME_TROPIC || tr.theme == THEME_HARBOR) sea_band(tr, th, off, baseY);
    for (int x = 0; x < W; ++x) {
        int col = (x + offN) & M;
        int h = tr.nearH[col];
        if (tr.theme == THEME_HARBOR && hash32(((col + 5) / 16) * 7 + 3) % 3 == 0) h = 0;   // gaps in the stacks: the sea shows through
        int top = baseY - h;
        uint16_t* p = fb + x;
        for (int y = top < 0 ? 0 : top; y < baseY + 2 && y < H; ++y) {
            int alt = baseY - y;
            if (alt < 0) alt = 0;
            uint16_t c = s_nearGrad[alt > 127 ? 127 : alt];
            if (tr.theme == THEME_CITY) {
                if (alt >= h - 1) c = th.nearHi;
                else if ((col % 5) == 1 && (y % 5) == 1 && (hash32(col * 7 + y) & 3) == 0) c = rgb(255, 200, 120);
            } else if (tr.theme == THEME_GLACIER) {
                // conifer belt melting into the mountain haze: no hard dark edge
                int dd = h - alt;                                   // 0 at the treeline
                int hz = 70;
                if (dd < 7) hz = 70 + (7 - dd) * 18 + ((((x + y) & 1) ? 10 : -10));
                c = lerp(c, th.fog, hz > 230 ? 230 : hz < 0 ? 0 : hz);
            } else if (alt >= h - 1) c = th.nearHi;
            p[y * W] = c;
        }
    }
    for (int y = baseY + 2; y < H; ++y) hline(0, W, y, th.ground);
}

// ------------------------------------------------------------------ road
float s_time = 0;

void draw_segment_rows(const PSeg& ps, int maxy, const Theme& th, const Seg& sg) {
    bool light = ((ps.idx / RUMBLE_LEN) & 1) == 0;
    int f = ps.fog;
    bool altG = sg.flags & SEGF_ALT;       // sand / ice / water sections
    const int tid = (int)(&th - g_themes);
    uint16_t g1 = altG ? th.alt1 : th.grass1, g2 = altG ? th.alt2 : th.grass2;
    uint16_t grass = lerp(light ? g1 : g2, th.fog, f);
    uint16_t rumble = lerp(light ? th.rumble1 : th.rumble2, th.fog, f);
    uint16_t road = lerp(light ? th.road1 : th.road2, th.fog, f);
    uint16_t lane = lerp(th.lane, th.fog, f);
    uint16_t shoulder = lerp(th.shoulder, th.fog, f);
    bool start = sg.flags & SEGF_START;
    uint16_t chkA = lerp(rgb(250, 250, 250), th.fog, f), chkB = lerp(rgb(20, 20, 24), th.fog, f);

    int yTop = (int)ceilf(ps.y2);
    int yBot = (int)ceilf(ps.y1);
    if (yTop < 0) yTop = 0;
    if (yBot > maxy) yBot = maxy;
    if (yBot > H) yBot = H;
    float dy = ps.y1 - ps.y2;
    if (dy < 0.001f) return;
    float invDy = 1.0f / dy;
    for (int y = yTop; y < yBot; ++y) {
        float t = (y - ps.y2) * invDy;
        float cx = ps.x2 + (ps.x1 - ps.x2) * t;
        float w = ps.w2 + (ps.w1 - ps.w2) * t;
        float rw = w / 6.0f;
        float lw = w / 34.0f;
        uint16_t* row = fb + y * W;
        auto span = [&](float a, float b, uint16_t c) {
            int ia = (int)a, ib = (int)b;
            if (ia < 0) ia = 0;
            if (ib > W) ib = W;
            for (int x = ia; x < ib; ++x) row[x] = c;
        };
        float l0 = cx - w - rw, l1 = cx - w, r0 = cx + w, r1 = cx + w + rw;
        span(0, l0, grass);
        span(l0, l1, rumble);
        if (start) {
            int ia = (int)l1 < 0 ? 0 : (int)l1, ib = (int)r0 > W ? W : (int)r0;
            float cell = (2 * w) / 14.0f;
            int rowPhase = (int)(t * 2) + ps.idx;
            for (int x = ia; x < ib; ++x) row[x] = ((((int)((x - l1) / cell)) + rowPhase) & 1) ? chkA : chkB;
        } else {
            span(l1, r0, road);
            // solid edge lines
            span(l1 + rw * 0.35f, l1 + rw * 0.35f + lw * 1.2f + 1, shoulder);
            span(r0 - rw * 0.35f - lw * 1.2f - 1, r0 - rw * 0.35f, shoulder);
            if (light) {
                for (int k = 1; k < LANES; ++k) {
                    float lx = l1 + (2 * w) * k / LANES;
                    span(lx - lw, lx + lw + 0.5f, lane);
                }
            }
        }
        span(r0, r1, rumble);
        span(r1, W, grass);
        if (!start && ps.pit >= PIT_ENTRY && ps.pit <= PIT_END) {                  // pit apron (v1.2): outside the road, behind a concrete wall
            float taper = pitTaper(ps.pit);
            float ax0 = cx + w * 1.02f, ax1 = cx + w * (1.02f + 1.02f * taper);
            bool pulse = (ps.idx + (int)(s_time * 6)) & 1;
            uint16_t ac = rgb(62, 68, 84);
            if (ps.idx >= PIT_BAY_NITRO0 && ps.idx <= PIT_BAY_NITRO1) ac = pulse ? rgb(24, 188, 232) : rgb(22, 134, 178);
            else if (ps.idx >= PIT_BAY_CHASSIS0 && ps.idx <= PIT_BAY_CHASSIS1) ac = pulse ? rgb(242, 192, 34) : rgb(192, 148, 28);
            else if (ps.idx >= PIT_BAY_ENGINE0 && ps.idx <= PIT_BAY_ENGINE1) ac = pulse ? rgb(226, 56, 52) : rgb(168, 42, 46);
            span(ax0, ax1, lerp(ac, th.fog, f));
            span(ax1 - lw, ax1 + lw, lerp(rgb(245, 245, 245), th.fog, f));
            if (ps.pit >= PIT_WALL_FIRST && ps.pit <= PIT_WALL_END)
                span(cx + w * (PIT_WALL_X - 0.035f), cx + w * (PIT_WALL_X + 0.035f), lerp((ps.idx & 1) ? rgb(245, 245, 245) : rgb(220, 56, 48), th.fog, f));
        }
        // ground texture: world-fixed dashes (tufts, ripples, wind streaks, cracks, glints)
        if (ps.fog < 215 && tid != THEME_CITY) {
            int j8 = (int)(t * 8);
            if ((j8 & 1) == 0) {
                for (int side = 0; side < 2; ++side)
                    for (int k = 0; k < 2; ++k) {
                        uint32_t hk = hash32((uint32_t)(ps.idx * 7349 + (j8 >> 1) * 131 + side * 977 + k * 31));
                        if (hk % 5 >= 2) continue;
                        float u = 1.45f + ((hk >> 8) & 255) / 255.0f * 6.0f;
                        float len = w * (0.05f + ((hk >> 16) & 7) * 0.02f);
                        if (len < 1.2f) continue;
                        float x0 = side ? cx + w * u : cx - w * u - len;
                        bool pale = (hk >> 20) & 1;
                        uint16_t pc;
                        switch (tid) {
                        case THEME_TROPIC: pc = altG ? (pale ? lerp(grass, rgb(255, 250, 235), 90) : scale(grass, 226)) : (pale ? lerp(grass, rgb(190, 232, 120), 80) : scale(grass, 210)); break;
                        case THEME_GLACIER: pc = altG ? (pale ? lerp(grass, rgb(255, 255, 255), 120) : lerp(grass, rgb(70, 120, 190), 80)) : (pale ? lerp(grass, rgb(255, 255, 255), 130) : lerp(grass, rgb(150, 176, 214), 90)); break;
                        case THEME_HARBOR: pc = altG ? (pale ? lerp(grass, rgb(255, 196, 110), 170) : lerp(grass, rgb(70, 110, 170), 70)) : (pale ? lerp(grass, rgb(130, 142, 164), 60) : scale(grass, 170)); break;
                        default: pc = pale ? lerp(grass, rgb(200, 226, 110), 70) : scale(grass, 208); break;
                        }
                        span(x0, x0 + len, lerp(pc, th.fog, f));
                    }
            }
        }
    }
}

uint16_t s_tmpPal[16];

const uint16_t* lit_palette(const uint16_t* base, uint16_t emissive, int ambient, uint16_t fogc, int fog) {
    for (int i = 0; i < 16; ++i) {
        uint16_t c = base[i];
        bool em = emissive & (1 << i);
        if (!em && ambient < 256) c = scale(c, ambient);
        s_tmpPal[i] = lerp(c, fogc, em ? fog / 2 : fog);
    }
    return s_tmpPal;
}

// pit lane scenery: entry board, canopy over the engine bay, two mechanics and the lollipop man
void draw_pit_scenery(const Race& r, const PSeg& ps) {
    if (ps.fog > 230 || ps.w1 < 14 || ps.w1 > 240) return;
    const float w = ps.w1;
    const int sy = (int)ps.y1;
    auto px = [&](int x, int y, uint16_t c) { if (x >= 0 && x < W && y >= 0 && y < ps.clip) fb[y * W + x] = c; };
    auto rect = [&](int x0, int y0, int x1, int y1, uint16_t c) { for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) px(x, y, c); };
    auto fogc = [&](uint16_t c) { return lerp(c, g_themes[r.track.theme].fog, ps.fog); };
    if (ps.pit == PIT_ENTRY + 1) {                          // entry board with a blinking amber beacon
        int x = (int)(ps.x1 + w * 1.06f), bw = (int)(w * 0.20f) + 3, bh = (int)(w * 0.12f) + 3;
        rect(x + bw / 2 - 1, sy - bh - bh / 2, x + bw / 2 + 1, sy, fogc(rgb(140, 145, 155)));
        rect(x, sy - bh - bh / 2, x + bw, sy - bh / 2, fogc(rgb(24, 148, 210)));
        rect(x + 1, sy - bh - bh / 2 + 1, x + bw - 1, sy - bh / 2 - bh / 3, fogc(rgb(250, 250, 250)));
        rect(x + bw / 4, sy - bh - bh / 2 + 2, x + bw * 3 / 4, sy - bh / 2 - bh / 3 - 1, fogc(rgb(24, 148, 210)));
        if (((int)(s_time * 6)) & 1) rect(x + bw / 2 - 2, sy - bh - bh / 2 - 4, x + bw / 2 + 2, sy - bh - bh / 2 - 1, fogc(rgb(255, 195, 40)));
    }
    if (ps.idx == PIT_BAY_ENGINE0 + 1 || ps.idx == PIT_BAY_ENGINE1 - 1) {      // canopy posts over the engine bay
        int x = (int)(ps.x1 + w * 1.98f), hh = (int)(w * 0.22f) + 4;
        uint16_t roof = rgb(228, 48, 48);
        rect(x, sy - hh, x + 2, sy, fogc(rgb(180, 186, 200)));
        rect(x - (int)(w * 0.9f), sy - hh - 3, x + 3, sy - hh + 1, fogc(roof));
        for (int k = x - (int)(w * 0.9f); k < x + 3; k += 8) rect(k, sy - hh - 3, k + 4, sy - hh + 1, fogc(rgb(248, 248, 252)));
    }
    if (ps.idx == PIT_BAY_ENGINE0 + 2 || ps.idx == PIT_BAY_ENGINE0 + 6) {      // crew: two mechanics (one each side of the car) step in when the car is serviced
        int h = (int)(w * 0.14f) + 6, mw = h * 42 / 100 + 2;
        bool work = r.pitting && r.activeBay == 3;
        int bob = work ? ((int)(s_time * 14) & 1) : 0;
        for (int m = 0; m < 2; ++m) {
            float ux = m == 0 ? (work ? 1.30f : 1.14f) : (work ? 1.66f : 1.82f);       // road half-widths
            int mx = (int)(ps.x1 + w * ux) - mw / 2, my = sy - h + bob;
            for (int y = my; y < sy; ++y) {
                float rel = (float)(y - my) / h;
                uint16_t c = rel < 0.25f ? rgb(245, 245, 250) : rel < 0.68f ? rgb(230, 52, 46) : rgb(34, 38, 52);
                for (int x = mx; x < mx + mw; ++x) px(x, y, fogc(c));
            }
        }
        if (ps.idx == PIT_BAY_ENGINE0 + 2) {                                           // lollipop man: STOP (red) / READY (amber) / GO (green)
            int lx = (int)(ps.x1 + w * 1.08f), ly = sy - (int)(h * 1.30f), rad = mw * 3 / 4 + 2;
            uint16_t sc = r.lollipop == 3 ? rgb(52, 242, 115) : r.lollipop == 2 ? rgb(255, 210, 48) : rgb(245, 52, 48);
            rect(lx, ly, lx + 1, sy, fogc(rgb(210, 215, 225)));
            for (int dy = -rad; dy <= rad; ++dy) for (int dx = -rad; dx <= rad; ++dx) if (dx * dx + dy * dy <= rad * rad) px(lx + dx, ly + dy, fogc(sc));
        }
    }
}

void draw_prop(const Race& r, const PSeg& ps, const Prop& pr) {
    const Theme& th = g_themes[r.track.theme];
    const Sprite& sp = g_sprites[pr.spr];
    float sc = ps.s1;
    float destW = sp.worldW * sc * W / 2;
    if (destW < 1.0f) return;
    float destH = destW * sp.h / sp.w;
    float sx = ps.x1 + sc * pr.offset * ROAD_W * W / 2;
    float anchor = pr.offset < -0.01f ? -1.0f : (pr.offset > 0.01f ? 0.0f : -0.5f);
    int x = (int)(sx + destW * anchor);
    int y = (int)(ps.y1 - destH);
    int dw = (int)(destW + 0.5f), dh = (int)(destH + 0.5f);
    if (x >= W || x + dw < 0 || y >= ps.clip) return;
    const uint16_t* pal = lit_palette(sprite_palette(pr.spr, r.track.theme), sp.emissive, th.ambient, th.fog, ps.fog);
    blit(sp, pal, x, y, dw, dh, ps.clip, pr.flip);
    int tid = r.track.theme;
    if ((tid == THEME_CITY || tid == THEME_HARBOR) && pr.spr == SPR_LAMP && dw > 3 && ps.fog < 200) {
        float hx = pr.flip ? (60 - 46) : 46;
        int gx = x + (int)(hx * destW / 60), gy = y + (int)(18 * destH / 180);
        uint16_t gc = tid == THEME_HARBOR ? rgb(150, 100, 44) : rgb(150, 120, 60);
        if (gy < ps.clip) glowCircle(gx, gy, (int)(destW * 0.45f) + 1, scale(gc, 256 - ps.fog));
    }
    if (tid == THEME_HARBOR && pr.spr == SPR_LIGHTHOUSE && dw > 2 && ps.fog < 235) {
        int gx = x + dw / 2, gy = y + (int)(41 * destH / 230);
        if (gy < ps.clip) glowCircle(gx, gy, (int)(destW * 0.9f) + 2, scale(rgb(170, 150, 80), 256 - ps.fog));
    }
}

void draw_car(const Race& r, const Car& c, int x, int y, int dw, int dh, int clip, int fog, bool isPlayerView) {
    const Theme& th = g_themes[r.track.theme];
    float sv = c.steerVis;
    int amb = th.ambient < 256 ? th.ambient + 40 : 256;
    if (car_art_ready()) {
        // true colour pictures: rear view, or the three-quarter rear view in hard turns / after an impact (never deformed)
        // stage: 0 rear view, 1..CAR_ART_MID intermediate pictures, CAR_ART_MID+1 three-quarter view
        constexpr bool kUseTq = true;       // rear three-quarter pictures (set false to keep the rear view in turns)
        int stage = 0; bool flip = false;
        if (!kUseTq) {}
        else if (c.isPlayer && r.spinT > 0 && r.spinDir != 0) { stage = CAR_ART_MID + 1; flip = r.spinDir > 0; }
        else {
            static const float kStep[CAR_ART_MID + 1] = { 0.09f, 0.22f, 0.35f, 0.49f, 0.63f, 0.78f };   // regular angular steps of the rigid rotation
            // the body only reaches the strongest angle when turning hard AND fast; slow or gentle turns stay on a middle picture
            float pct = c.speed / MAX_SPEED; if (pct > 1) pct = 1; if (pct < 0) pct = 0;
            float a = fabsf(sv) * (0.22f + 0.78f * pct * pct);
            stage = 0;
            while (stage <= CAR_ART_MID && a >= kStep[stage]) ++stage;
            flip = sv > 0;
        }
        const bool tq = stage > 0;                          // wide pictures
        const float u = (float)stage / (CAR_ART_MID + 1);   // 0 rear .. 1 three-quarter
        int dhA = dw * CAR_ART_H / CAR_ART_W;
        // the tyres touch the ground line of the old sprites (the picture has a diffuser below the tyres)
        int ya = y + dh * 52 / 58 - dhA * 97 / 100;
        int pw = tq ? dw * CAR_ART3Q_W / CAR_ART_W : dw, px0 = tq ? x - (pw - dw) / 2 : x;
        // ground shadow: follows the silhouette of the picture (lifted on the far side in turns)
        car_shadow(c.look, stage, flip, x, ya, dw, dhA, clip);
        blit_car(c.look, stage, flip, x, ya, dw, dhA, clip, amb, th.fog, fog);
        // lamps: lit when braking (strong) or at night (dim), on the lamp boxes of the picture
        if ((c.braking || th.ambient < 256) && dw > 8 && fog < 200) {
            const int picW = tq ? CAR_ART3Q_W : CAR_ART_W;
            int lb[8];                                       // lamp boxes slide from the rear picture (centred in the wide frame) to the 3/4 ones
            for (int q = 0; q < 8; ++q) {
                if (!tq) { lb[q] = kLampRear[q]; continue; }
                const int a0 = kLampRear[q] + ((q & 1) ? 0 : (CAR_ART3Q_W - CAR_ART_W) / 2), a1 = kLamp3q[c.look % CAR_ART_COUNT][q];
                lb[q] = a0 + (int)((a1 - a0) * u + (a1 >= a0 ? 0.5f : -0.5f));
            }
            uint16_t col = c.braking ? rgb(255, 36, 24) : rgb(150, 14, 14);
            int alpha = c.braking ? 190 : 110;
            for (int k = 0; k < 2; ++k) {
                int bx0 = lb[k * 4], by0 = lb[k * 4 + 1], bx1 = lb[k * 4 + 2], by1 = lb[k * 4 + 3];
                if (flip) { int t0 = picW - bx1, t1 = picW - bx0; bx0 = t0; bx1 = t1; }
                int sx0 = px0 + bx0 * pw / picW, sx1 = px0 + bx1 * pw / picW;
                int sy0 = ya + by0 * dhA / CAR_ART_H, sy1 = ya + by1 * dhA / CAR_ART_H;
                if (sy1 > clip) sy1 = clip;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) continue;
                blendRect(sx0, sy0, sx1 - sx0, sy1 - sy0, scale(col, 256 - fog), alpha);
                int rad = (sx1 - sx0) * 7 / 10 + 1;
                int gy = (sy0 + sy1) / 2;
                if (gy < clip) glowCircle((sx0 + sx1) / 2, gy, rad, scale(c.braking ? rgb(140, 20, 20) : rgb(70, 10, 10), 256 - fog));
            }
        }
        return;
    }
    int id = SPR_CAR;
    if (c.isPlayer && r.spinT > 0 && r.spinDir != 0) id = r.spinDir < 0 ? SPR_CAR_LEFT3Q : SPR_CAR_RIGHT3Q;   // yawed by an impact
    else if (sv < -0.9f) id = SPR_CAR_LEFT3Q;
    else if (sv < -0.3f) id = SPR_CAR_LEFT;
    else if (sv > 0.9f) id = SPR_CAR_RIGHT3Q;
    else if (sv > 0.3f) id = SPR_CAR_RIGHT;
    const Sprite& sp = g_sprites[id];
    const float sc = dw / 112.0f;                         // screen pixels per sprite pixel
    if (sp.anchorX) { x += (int)((56 - sp.anchorX) * sc); dw = (int)(sp.w * sc + 0.5f); }   // wider three-quarter sprites
    uint16_t base[16];
    car_palette(base, c.livery, c.braking);
    const uint16_t* pal = lit_palette(base, sp.emissive, amb, th.fog, fog);
    blit(sp, pal, x, y, dw, dh, clip, false);
    if ((c.braking || th.ambient < 256) && dw > 8 && fog < 200) {
        uint16_t gc = c.braking ? rgb(140, 20, 20) : rgb(70, 10, 10);
        int ly = y + 31 * dh / 58;
        if (ly < clip) {
            int rad = (int)(112 * sc / (isPlayerView ? 8 : 7)) + 1;
            glowCircle(x + (int)((sp.lightL ? sp.lightL : 20) * sc), ly, rad, scale(gc, 256 - fog));
            glowCircle(x + (int)((sp.lightR ? sp.lightR : 92) * sc), ly, rad, scale(gc, 256 - fog));
        }
    }
}

} // namespace

// =================================================================== world
void render_world(const Race& r, float time) {
    s_time = time;
    const Track& tr = r.track;
    const Theme& th = g_themes[tr.theme];
    if (s_cacheTheme != tr.theme) build_theme_cache(tr.theme);
    const Car& pl = r.player();
    const int N = tr.count();

    float camZ = fmodf(r.cameraZ(), tr.length);
    if (camZ < 0) camZ += tr.length;
    int baseIdx = (int)(camZ / SEG_LEN) % N;
    float basePct = fmodf(camZ, SEG_LEN) / SEG_LEN;
    const Seg& baseSeg = tr.segs[baseIdx];

    float plZ = r.carZ(pl);
    int plIdx = (int)(plZ / SEG_LEN) % N;
    float plPct = fmodf(plZ, SEG_LEN) / SEG_LEN;
    const Seg& plSeg = tr.segs[plIdx];
    float playerY = plSeg.y1 + (plSeg.y2 - plSeg.y1) * plPct;

    float shakeX = r.shake > 0 ? sinf(time * 90) * r.shake * 120 : 0;
    float camX = pl.x * ROAD_W + shakeX;
    float camY = playerY + CAM_HEIGHT;

    draw_sky(r, time);
    draw_layers(r, HORIZON + 4);

    // project + draw road, front to back
    int maxy = H;
    float x = 0, dx = -(baseSeg.curve * basePct);
    for (int n = 0; n < DRAW_DIST; ++n) {
        int gi = baseIdx + n;
        int i = gi % N;
        const Seg& sg = tr.segs[i];
        PSeg& ps = s_ps[n];
        ps.idx = i;
        ps.pit = pitIdx(i, N);
        ps.clip = maxy;
        float d = (float)n / DRAW_DIST;
        ps.fog = (int)((1.0f - 1.0f / expf(d * d * th.fogDensity)) * 256);
        float z1 = gi * SEG_LEN - camZ, z2 = (gi + 1) * SEG_LEN - camZ;
        ps.valid = z1 > 1.0f;
        if (z2 <= 1.0f) { x += dx; dx += sg.curve; continue; }
        if (z1 < 1.0f) z1 = 1.0f;
        float s1 = CAM_DEPTH / z1, s2 = CAM_DEPTH / z2;
        ps.s1 = s1; ps.s2 = s2;
        ps.x1 = W / 2 + s1 * (-(camX - x)) * W / 2;
        ps.y1 = H / 2 - s1 * (sg.y1 - camY) * H / 2;
        ps.w1 = s1 * ROAD_W * W / 2;
        ps.x2 = W / 2 + s2 * (-(camX - x - dx)) * W / 2;
        ps.y2 = H / 2 - s2 * (sg.y2 - camY) * H / 2;
        ps.w2 = s2 * ROAD_W * W / 2;
        x += dx;
        dx += sg.curve;
        if (z1 <= CAM_DEPTH || ps.y2 >= ps.y1 || ps.y2 >= maxy) continue;
        draw_segment_rows(ps, maxy, th, sg);
        maxy = (int)ceilf(ps.y2);
        if (maxy < 0) maxy = 0;
    }

    // sprites and cars, back to front
    int plN = -1;
    for (int n = 0; n < DRAW_DIST; ++n) if (s_ps[n].idx == plIdx) { plN = n; break; }

    for (int n = DRAW_DIST - 1; n > 0; --n) {
        const PSeg& ps = s_ps[n];
        if (!ps.valid) continue;
        const Seg& sg = tr.segs[ps.idx];
        for (int j = 0; j < sg.propCount; ++j) draw_prop(r, ps, tr.props[sg.propFirst + j]);
        if (ps.pit >= PIT_ENTRY && ps.pit <= PIT_END) draw_pit_scenery(r, ps);
        for (int k = 0; k < CAR_COUNT; ++k) {
            const Car& c = r.cars[k];
            if (&c == &pl) continue;
            float cz = r.carZ(c);
            if ((int)(cz / SEG_LEN) % N != ps.idx) continue;
            float pct = fmodf(cz, SEG_LEN) / SEG_LEN;
            float sc = ps.s1 + (ps.s2 - ps.s1) * pct;
            float sx = ps.x1 + (ps.x2 - ps.x1) * pct + sc * c.x * ROAD_W * W / 2;
            float sy = ps.y1 + (ps.y2 - ps.y1) * pct;
            float dw = 700.0f * sc * W / 2;
            if (dw < 2) continue;
            float dh = dw * 58 / 112;
            draw_car(r, c, (int)(sx - dw / 2), (int)(sy - dh), (int)dw, (int)dh, ps.clip, ps.fog, false);
        }
        if (n == plN) {
            // player car: fixed on screen, bouncing on rough surfaces
            float bounce = 0;
            float pct = pl.speed / MAX_SPEED;
            if (r.offroad > 0) bounce = (float)((int)(time * 40) & 1) * 2.0f * r.offroad * (pct > 0.05f ? 1 : 0);
            else bounce = ((int)(time * 12) & 1) * (pct > 0.3f ? 1.0f : 0.0f);
            int px = W / 2 - 56 + (int)(shakeX * 0.01f), py = H - 58 - 11 - (int)bounce;
            // night headlights on the tarmac
            if (th.ambient < 256) {
                for (int y = H / 2 + 22; y < py + 20; ++y) {
                    float t = (float)(y - (H / 2 + 22)) / (py + 20 - (H / 2 + 22));
                    int hw = (int)(18 + t * 70);
                    int k = (int)(60 * t * (1.0f - t) * 4);
                    if (k <= 0) continue;
                    uint16_t add_c = scale(rgb(90, 80, 50), k);
                    uint16_t* row = fb + y * W;
                    for (int xx = W / 2 - hw; xx < W / 2 + hw; ++xx) {
                        if ((unsigned)xx >= (unsigned)W) continue;
                        int edge = hw - abs(xx - W / 2);
                        row[xx] = add(row[xx], edge < 12 ? scale(add_c, edge * 21) : add_c);
                    }
                }
            }
            if (r.spinT > 0) { px += (int)(sinf(time * 70) * 3.0f * r.spinT / 0.55f); py -= (int)(r.spinT > 0.4f ? 2 : 0); }
            draw_car(r, pl, px, py, 112, 58, H, 0, true);
            if (r.nitroOn) {
                for (int e = 0; e < 2; ++e) {
                    int ex = px + (e ? 76 : 36), ey = py + 47;
                    int len = 8 + (int)(hash32((uint32_t)(time * 60) + e * 17) % 10);
                    glowCircle(ex, ey + 3, 10, rgb(60, 40, 120));
                    fillTri(ex - 4, ey, ex + 4, ey, ex, ey + len, rgb(255, 140, 40));
                    fillTri(ex - 2, ey, ex + 2, ey, ex, ey + len * 2 / 3, rgb(255, 250, 180));
                    fillTri(ex - 1, ey, ex + 1, ey, ex, ey + 3, rgb(140, 200, 255));
                }
            }
        }
    }

    // nitro speed streaks
    if (r.nitroOn) {
        for (int k = 0; k < 14; ++k) {
            uint32_t hsh = hash32((uint32_t)(time * 30) * 131 + k);
            float a = (hsh % 6283) / 1000.0f;
            float r0 = 90 + (hsh >> 13) % 60, r1 = r0 + 30 + (hsh >> 20) % 40;
            int x0 = W / 2 + (int)(cosf(a) * r0 * 1.4f), y0 = H / 2 + (int)(sinf(a) * r0);
            int x1 = W / 2 + (int)(cosf(a) * r1 * 1.4f), y1 = H / 2 + (int)(sinf(a) * r1);
            line(x0, y0, x1, y1, rgb(230, 240, 255));
        }
    }
}

// =================================================================== particles
namespace {
// kind: 0 fading square, 1 spark streak, 2 solid chip, 3 growing soft puff
struct Particle { float x, y, vx, vy, life, max; uint16_t c; uint8_t size, kind; float g, grow; };
constexpr int MAXP = 260;
Particle* s_p = nullptr;          // PSRAM (10 KB less internal RAM)
int s_pn = 0;
uint32_t s_seed = 99;
float s_flash = 0; uint16_t s_flashCol = 0;
float prand() { s_seed ^= s_seed << 13; s_seed ^= s_seed >> 17; s_seed ^= s_seed << 5; return (s_seed & 0xFFFF) / 65536.0f; }

void spawn(float x, float y, float vx, float vy, float life, uint16_t c, int size, int kind = 0, float g = 40.0f, float grow = 3.0f) {
    if (!s_p) s_p = (Particle*)plat::allocLarge(sizeof(Particle) * MAXP);
    if (!s_p || s_pn >= MAXP) return;
    s_p[s_pn++] = { x, y, vx, vy, life, life, c, (uint8_t)size, (uint8_t)kind, g, grow };
}
} // namespace

void fx_clear() { s_pn = 0; s_flash = 0; }

void fx_burst(int x, int y, int n, uint16_t c, float speed, float life) {
    for (int i = 0; i < n; ++i) {
        float a = prand() * 6.2831f, v = speed * (0.3f + prand());
        spawn((float)x, (float)y, cosf(a) * v, sinf(a) * v - speed * 0.5f, life * (0.5f + prand()), c, 1 + (int)(prand() * 2));
    }
}

namespace {
void sparks(int x, int y, int n, float v, int dirX) {
    for (int i = 0; i < n * 3 / 2; ++i) {
        float a = prand() * 6.2831f, sp = v * (0.35f + prand());
        float vx = cosf(a) * sp + dirX * v * 0.5f, vy = sinf(a) * sp - v * 0.55f;
        spawn((float)x, (float)y, vx, vy, 0.25f + prand() * 0.35f, prand() < 0.4f ? rgb(255, 255, 220) : rgb(255, 190 + (int)(prand() * 40), 60), 1, 1, 260.0f);
    }
}
void chips(int x, int y, int n, float v, uint16_t c1, uint16_t c2, int dirX, int sizeMax = 3) {
    for (int i = 0; i < n; ++i) {
        float a = prand() * 6.2831f, sp = v * (0.3f + prand());
        spawn((float)x, (float)y, cosf(a) * sp + dirX * v * 0.4f, sinf(a) * sp - v * 0.6f, 0.5f + prand() * 0.5f, prand() < 0.5f ? c1 : c2, 3 + (int)(prand() * (sizeMax - 1)), 2, 280.0f);
    }
}
void puff(int x, int y, int n, uint16_t c, int size, float spread, float rise, float life = 0.9f, float grow = 5.0f) {
    for (int i = 0; i < n; ++i)
        spawn(x + (prand() - 0.5f) * spread, y + (prand() - 0.5f) * spread * 0.5f, (prand() - 0.5f) * 40, -rise * (0.4f + prand()), life * (0.6f + prand() * 0.6f), c, size + 2, 3, -8.0f, grow);
}
void leaves(int x, int y, int n, uint16_t c1, uint16_t c2, float v) {
    for (int i = 0; i < n; ++i) {
        float a = prand() * 6.2831f, sp = v * (0.3f + prand());
        spawn((float)x, (float)y, cosf(a) * sp, sinf(a) * sp - v * 0.8f, 0.9f + prand() * 0.7f, prand() < 0.5f ? c1 : c2, 3, 2, 45.0f);
    }
}
} // namespace

void fx_flash(uint16_t c, float strength) { s_flash = strength; s_flashCol = c; }

// impact effects, chosen by what was hit
void fx_emit(const Race& r) {
    const RaceEvents& e = r.ev;
    if (!e.hit) return;
    const bool cold = r.track.theme == THEME_ALPINE || r.track.theme == THEME_GLACIER;
    const int side = e.hitSide ? e.hitSide : 1;
    const float pw = e.hitPower;
    const int ix = W / 2 + side * 52, iy = H - 34;
    const int nS = 4 + (int)(pw * 10);
    switch (e.hit) {
    case HIT_CAR_REAR:
        sparks(W / 2, H - 52, nS, 120, 0);
        chips(W / 2 + side * 20, H - 40, 5, 90, rgb(220, 30, 30), e.hitColor, side, 2);
        puff(W / 2, H - 62, 3, rgb(150, 150, 156), 4, 30, 25, 0.8f);
        break;
    case HIT_CAR_SIDE:
        for (int k = 0; k < 6 + (int)(pw * 8); ++k)     // sparks sliding along the contact line
            spawn((float)ix, H - 56.0f + prand() * 30, side * (30 + prand() * 80), -30 - prand() * 50, 0.2f + prand() * 0.3f,
                  prand() < 0.5f ? rgb(255, 255, 210) : rgb(255, 180, 50), 1, 1, 240.0f);
        chips(ix, H - 44, 4, 70, e.hitColor, rgb(230, 230, 236), side, 2);
        break;
    default: switch (e.hitSpr) {
        case SPR_PALM: case SPR_TREE: case SPR_PINE:
            chips(ix, iy, 6 + (int)(pw * 6), 130, rgb(120, 80, 46), rgb(170, 120, 70), -side);
            leaves(ix, iy - 10, 8 + (int)(pw * 8), rgb(40, 130, 60), rgb(90, 180, 80), 90);
            if (cold || e.hitSpr == SPR_PINE) puff(ix, iy - 20, 5, rgb(240, 246, 255), 5, 30, 20, 1.0f, 6.0f);   // snow shaken off the branches
            puff(ix, iy, 3, rgb(150, 120, 80), 4, 18, 18);
            break;
        case SPR_BUSH: case SPR_FLOWERS: case SPR_DUNEGRASS:
            leaves(ix, iy - 6, 10, rgb(50, 140, 60), rgb(110, 190, 80), 80);
            if (e.hitSpr == SPR_FLOWERS) leaves(ix, iy - 8, 8, rgb(240, 60, 80), rgb(255, 220, 60), 70);
            if (e.hitSpr == SPR_DUNEGRASS) puff(ix, iy, 4, rgb(236, 214, 160), 4, 26, 16);
            break;
        case SPR_HAYBALE:
            leaves(ix, iy - 6, 18, rgb(236, 196, 80), rgb(250, 226, 130), 100);
            puff(ix, iy - 4, 4, rgb(226, 190, 100), 5, 30, 20);
            break;
        case SPR_SNOWMAN:
            puff(ix, iy - 10, 8, rgb(250, 252, 255), 6, 36, 25, 1.0f, 6.0f);
            chips(ix, iy - 14, 3, 80, rgb(255, 130, 30), rgb(30, 30, 40), side, 2);
            break;
        case SPR_PARASOL:
            leaves(ix, iy - 14, 10, rgb(238, 60, 60), rgb(252, 252, 252), 90);
            break;
        case SPR_ROCK:
            chips(ix, iy, 10 + (int)(pw * 6), 150, rgb(120, 116, 128), rgb(176, 172, 184), -side, 4);
            sparks(ix, iy - 4, 6, 110, -side);
            puff(ix, iy, 5, rgb(176, 160, 130), 5, 26, 22, 1.0f, 6.0f);
            break;
        case SPR_ICE:
            chips(ix, iy - 6, 12 + (int)(pw * 6), 140, rgb(200, 236, 252), rgb(120, 180, 232), -side, 3);
            for (int k = 0; k < 6; ++k) spawn(ix + (prand() - 0.5f) * 30, iy - 10 - prand() * 20, 0, -10, 0.5f, rgb(255, 255, 255), 1, 0, 0);
            puff(ix, iy - 4, 5, rgb(236, 246, 255), 5, 28, 20, 0.9f, 5.0f);
            break;
        case SPR_FENCE: case SPR_CRATES: case SPR_HUT:
            chips(ix, iy - 4, 10 + (int)(pw * 6), 150, rgb(100, 68, 40), rgb(176, 130, 80), -side, 4);
            puff(ix, iy, 4, rgb(190, 160, 120), 4, 24, 18);
            break;
        case SPR_BOARD_A: case SPR_BOARD_B: case SPR_BOARD_C: case SPR_BOARD_D: case SPR_BOARD_E: case SPR_BOARD_F:
            sparks(ix, iy - 4, nS + 4, 140, -side);
            chips(ix, iy - 10, 8, 120, rgb(250, 250, 250), rgb(210, 60, 60), -side, 3);
            break;
        case SPR_BUILDING_A: case SPR_BUILDING_B: case SPR_BUILDING_C: case SPR_WAREHOUSE: case SPR_CRANE:
        case SPR_LIGHTHOUSE: case SPR_GANTRY:
            chips(ix, iy - 4, 12 + (int)(pw * 8), 160, rgb(110, 114, 126), rgb(170, 174, 184), -side, 4);
            sparks(ix, iy - 8, 10, 150, -side);
            puff(ix, iy - 4, 8, rgb(150, 150, 160), 7, 40, 24, 1.1f, 7.0f);
            break;
        case SPR_CONTAINER: case SPR_CONTAINER2:
            chips(ix, iy - 4, 10 + (int)(pw * 6), 150, rgb(196, 56, 46), rgb(46, 100, 174), -side, 4);
            sparks(ix, iy - 8, 10, 150, -side);
            puff(ix, iy - 4, 6, rgb(150, 150, 160), 6, 36, 22, 1.0f, 6.0f);
            break;
        default:        // posts, lamps, barriers, chevrons
            sparks(ix, iy - 6, nS + 6, 150, -side);
            chips(ix, iy - 8, 6, 110, rgb(222, 40, 44), rgb(246, 246, 248), -side, 3);
            break;
        }
    }
    if (e.hit == HIT_HEAVY || e.hit == HIT_ROCK) fx_flash(rgb(255, 250, 235), 0.28f + 0.3f * pw);
    else if (e.hit == HIT_METAL || e.hit == HIT_WOOD || e.hit == HIT_ICE) fx_flash(rgb(255, 255, 255), 0.10f + 0.12f * pw);
}

void fx_update(const Race& r, float dt) {
    const Theme& th = g_themes[r.track.theme];
    float pct = r.player().speed / MAX_SPEED;
    int py = H - 58 - 3;
    if (r.offroad > 0.5f && pct > 0.1f) {
        for (int e = 0; e < 2; ++e)
            if (prand() < 0.8f) spawn(W / 2 + (e ? 38 : -38) + (prand() - 0.5f) * 14, (float)py + 52,
                                       (prand() - 0.5f) * 60, -20 - prand() * 50, 0.5f, lerp(th.grass2, rgb(120, 100, 70), 128), 2 + (int)(prand() * 2));
    }
    if (r.skid > 0.2f) {
        for (int e = 0; e < 2; ++e)
            if (prand() < r.skid) spawn(W / 2 + (e ? 38 : -38) + (prand() - 0.5f) * 10, (float)py + 52,
                                         (prand() - 0.5f) * 30, -30 - prand() * 30, 0.6f, rgb(200, 200, 210), 3);
    }
    if (r.nitroOn) {
        for (int e = 0; e < 2; ++e) spawn(W / 2 + (e ? 20 : -20), (float)py + 54, (prand() - 0.5f) * 20, 40 + prand() * 40, 0.2f, rgb(255, 190, 80), 1);
    }
    if (r.smokeT > 0 && prand() < 0.5f) {       // smoke from the damaged car
        float k = r.smokeT > 1.0f ? 1.0f : r.smokeT;
        spawn(W / 2 + (prand() - 0.5f) * 40, (float)py + 22, (prand() - 0.5f) * 14, -30 - prand() * 20, 0.8f + 0.5f * k, lerp(rgb(60, 60, 66), rgb(170, 170, 176), (int)(prand() * 160)), 4, 3, -6.0f, 6.0f);
    }
    if (r.damage > 0.3f) {                         // permanent damage: smoke, then black smoke, then flames
        float d = r.damage;
        float rate = (d - 0.3f) * 1.1f + (r.sputter > 0 ? 0.3f : 0.0f);
        if (prand() < rate) {
            int g = d > 0.7f ? 40 : 90;
            uint16_t c = lerp(rgb(d > 0.7f ? 22 : 70, d > 0.7f ? 22 : 70, d > 0.7f ? 24 : 76), rgb(150, 150, 156), (int)(prand() * (d > 0.7f ? 70 : 150)) + g - 40);
            spawn(W / 2 + (prand() - 0.5f) * 36, (float)py + 24, (prand() - 0.5f) * 16, -34 - prand() * 22, 0.7f + 0.5f * d, c, 4, 3, -6.0f, 6.0f);
        }
        if (d > 0.82f && prand() < (d - 0.8f) * 4.0f) {
            uint16_t c = prand() < 0.5f ? rgb(255, 150, 40) : rgb(255, 70, 20);
            spawn(W / 2 + (prand() - 0.5f) * 30, (float)py + 26, (prand() - 0.5f) * 26, -45 - prand() * 40, 0.35f, c, 1);
        }
        if (r.player().retired && prand() < 0.5f)
            spawn(W / 2 + (prand() - 0.5f) * 40, (float)py + 22, (prand() - 0.5f) * 10, -26 - prand() * 18, 1.2f, rgb(40, 40, 44), 4, 4, -6.0f, 8.0f);
    }
    if (s_flash > 0) s_flash -= dt * 2.6f;
    for (int i = 0; i < s_pn;) {
        Particle& p = s_p[i];
        p.life -= dt;
        if (p.life <= 0) { s_p[i] = s_p[--s_pn]; continue; }
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.vy += p.g * dt;
        if (p.kind == 3) { p.vx *= 1.0f - 1.5f * dt; }
        ++i;
    }
}

void fx_draw() {
    for (int i = 0; i < s_pn; ++i) {
        const Particle& p = s_p[i];
        float u = p.life / p.max;                    // 1 -> 0
        int t = (int)(u * 256);
        switch (p.kind) {
        case 1: {                                    // spark streak
            int x1 = (int)p.x, y1 = (int)p.y;
            int x0 = (int)(p.x - p.vx * 0.055f), y0 = (int)(p.y - p.vy * 0.055f);
            line(x0, y0, x1, y1, t > 120 ? p.c : lerp(p.c, rgb(160, 40, 10), 200 - t));
            break; }
        case 2: {                                    // solid chip, fades only at the end
            int s2 = p.size;
            uint16_t c = u < 0.25f ? lerp(p.c, rgb(0, 0, 0), 60) : p.c;
            if (u > 0.12f) fillRect((int)p.x - s2 / 2, (int)p.y - s2 / 2, s2, s2, c);
            break; }
        case 3: {                                    // soft puff growing and thinning
            int s3 = p.size + (int)((1.0f - u) * p.grow * 3);
            blendRect((int)p.x - s3 / 2, (int)p.y - s3 / 2, s3, s3, p.c, t * 5 / 6);
            break; }
        default: {
            int s = p.size + (int)((1.0f - u) * 3);
            blendRect((int)p.x - s / 2, (int)p.y - s / 2, s, s, p.c, t * 3 / 4);
        }
        }
    }
    if (s_flash > 0.01f) blendRect(0, 0, W, H, s_flashCol, (int)(s_flash * 256) > 160 ? 160 : (int)(s_flash * 256));
}

// =================================================================== HUD
void format_time(char* out, float t) {
    if (t < 0) t = 0;
    int cs = (int)(t * 100 + 0.5f);
    snprintf(out, 16, "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

void render_minimap(const Race& r, int x0, int y0, float time) {
    const Track& tr = r.track;
    blendRect(x0, y0, 64, 64, rgb(0, 0, 0), 120);
    rect(x0, y0, 64, 64, rgb(255, 255, 255));
    int cx = x0 + 32, cy = y0 + 32;
    for (int m = 0; m < MAP_N; ++m) {
        int a = m, b = (m + 1) % MAP_N;
        int xa = cx + tr.mapX[a], ya = cy + tr.mapY[a], xb = cx + tr.mapX[b], yb = cy + tr.mapY[b];
        line(xa + 1, ya + 1, xb + 1, yb + 1, rgb(20, 20, 30));
        line(xa, ya, xb, yb, rgb(210, 210, 220));
    }
    fillRect(cx + tr.mapX[0] - 2, cy + tr.mapY[0] - 1, 5, 3, rgb(255, 255, 255));
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < CAR_COUNT; ++k) {
            const Car& c = r.cars[k];
            if ((pass == 1) != c.isPlayer) continue;
            float f = r.carZ(c) / tr.length * MAP_N;
            int m = (int)f % MAP_N, m2 = (m + 1) % MAP_N;
            float u = f - (int)f;
            int px = cx + (int)(tr.mapX[m] + (tr.mapX[m2] - tr.mapX[m]) * u);
            int py = cy + (int)(tr.mapY[m] + (tr.mapY[m2] - tr.mapY[m]) * u);
            if (c.isPlayer) {
                bool blink = ((int)(time * 6) & 1);
                fillCircle(px, py, 3, rgb(0, 0, 0));
                fillCircle(px, py, 2, blink ? rgb(255, 255, 255) : c.livery.body);
            } else {
                fillRect(px - 1, py - 1, 3, 3, c.livery.body);
            }
        }
}

void render_tower(const Race& r, int x, int y) {
    int order[CAR_COUNT];
    for (int k = 0; k < CAR_COUNT; ++k) order[r.cars[k].place - 1] = k;
    blendRect(x, y, 62, CAR_COUNT * 9 + 3, rgb(0, 0, 0), 110);
    for (int i = 0; i < CAR_COUNT; ++i) {
        const Car& c = r.cars[order[i]];
        int yy = y + 2 + i * 9;
        if (c.isPlayer) blendRect(x, yy - 1, 62, 9, rgb(255, 255, 255), 70);
        char buf[4]; snprintf(buf, sizeof buf, "%d", i + 1);
        text(x + 2, yy, buf, c.isPlayer ? rgb(255, 230, 80) : rgb(200, 200, 210));
        fillRect(x + 12, yy + 1, 6, 6, c.livery.body);
        rect(x + 12, yy + 1, 6, 6, c.livery.stripe);
        { char nm[9]; snprintf(nm, sizeof nm, "%s", c.name); text(x + 21, yy, nm, c.isPlayer ? rgb(255, 230, 80) : rgb(240, 240, 240)); }
    }
}


void render_hud(const Race& r, float time) {
    const Car& pl = r.player();
    char buf[32];

    // position
    snprintf(buf, sizeof buf, "%d", pl.place);
    textFancy(8, 6, buf, rgb(255, 246, 120), rgb(255, 120, 20), rgb(30, 10, 10), 4);
    textFancy(8 + 34, 8, i18n::ordinal(pl.place), rgb(255, 246, 120), rgb(255, 140, 30), rgb(30, 10, 10), 2);
    snprintf(buf, sizeof buf, "/%d", CAR_COUNT);
    textOutline(8 + 36, 26, buf, rgb(255, 255, 255), rgb(0, 0, 0));
    render_tower(r, 6, 44);

    // lap + times
    snprintf(buf, sizeof buf, "%d/%d", r.playerLap(), r.laps);
    int lw = textWidth(buf, 3);
    { const char* ll = TR("LAP"); textOutline(W - 8 - lw - textWidth(ll) - 4, 12, ll, rgb(255, 255, 255), rgb(0, 0, 0)); }
    textFancy(W - 8 - lw, 6, buf, rgb(255, 255, 255), rgb(140, 200, 255), rgb(10, 20, 40), 3);
    char t1[16], t2[16];
    format_time(t1, r.raceTime - pl.lapStart);
    snprintf(buf, sizeof buf, "%s %s", TR("TIME"), t1);
    textOutline(W - 8 - textWidth(buf), 36, buf, rgb(255, 255, 255), rgb(0, 0, 0));
    if (pl.bestLap > 0) {
        format_time(t2, pl.bestLap);
        snprintf(buf, sizeof buf, "%s %s", TR("BEST"), t2);
        textOutline(W - 8 - textWidth(buf), 47, buf, rgb(255, 220, 90), rgb(0, 0, 0));
    }

    // minimap
    render_minimap(r, 6, H - 70, time);

    // speedometer
    const int cx = 282, cy = 204, rad = 32;
    blendCircle(cx, cy, rad + 4, rgb(0, 0, 0), 150);
    float kmh = r.speedKmh();
    const float a0 = 3.92699f, a1 = -0.785398f;   // 225deg .. -45deg
    for (int k = 0; k <= 30; ++k) {
        float f = k / 30.0f;
        float a = a0 + (a1 - a0) * f;
        float ca = cosf(a), sa = -sinf(a);
        bool major = (k % 5) == 0;
        uint16_t c = f < 0.6f ? rgb(230, 230, 240) : (f < 0.8f ? rgb(255, 210, 60) : rgb(255, 60, 60));
        if (f * 300 <= kmh) c = f < 0.6f ? rgb(90, 255, 140) : (f < 0.8f ? rgb(255, 230, 60) : rgb(255, 80, 60));
        int ri = major ? rad - 7 : rad - 4;
        line(cx + (int)(ca * ri), cy + (int)(sa * ri), cx + (int)(ca * rad), cy + (int)(sa * rad), c);
    }
    float f = kmh / 300.0f; if (f > 1) f = 1;
    float a = a0 + (a1 - a0) * f;
    int nx = cx + (int)(cosf(a) * (rad - 6)), ny = cy + (int)(-sinf(a) * (rad - 6));
    thickLine(cx, cy, nx, ny, 2, rgb(255, 50, 40));
    fillCircle(cx, cy, 3, rgb(240, 240, 240));
    snprintf(buf, sizeof buf, "%d", (int)kmh);
    textFancy(cx - textWidth(buf, 2) / 2, cy + 8, buf, rgb(255, 255, 255), rgb(170, 220, 255), rgb(0, 0, 0), 2);
    text(cx - 16, cy + 26, TR("KM/H"), rgb(180, 180, 200));
    snprintf(buf, sizeof buf, "%d", r.gear);
    textOutline(cx - 4, cy - 18, buf, rgb(255, 210, 60), rgb(0, 0, 0));

    // nitro gauge
    const int gx = 232, gy = 180, gh = 52;
    blendRect(gx - 2, gy - 2, 10, gh + 4, rgb(0, 0, 0), 150);
    int fill = (int)(r.nitro * gh);
    for (int y = 0; y < fill; ++y) {
        uint16_t c = lerp(rgb(40, 120, 255), rgb(120, 255, 255), y * 256 / gh);
        if (r.nitroOn && ((int)(time * 20) & 1)) c = add(c, rgb(60, 60, 60));
        hline(gx, gx + 6, gy + gh - 1 - y, c);
    }
    rect(gx - 2, gy - 2, 10, gh + 4, rgb(200, 220, 255));
    text(gx - 9, gy - 12, TR("N2O"), r.nitro > 0.99f ? rgb(120, 255, 255) : rgb(150, 170, 200));

    // engine health gauge (full when mint, empties as damage grows)
    {
        const int hx = 221;
        blendRect(hx - 2, gy - 2, 10, gh + 4, rgb(0, 0, 0), 150);
        float hp = 1.0f - r.damage;
        int hf = (int)(hp * gh);
        bool blink = r.damage > 0.8f && ((int)(time * 6) & 1);
        for (int y = 0; y < hf; ++y) {
            uint16_t c = hp > 0.5f ? lerp(rgb(240, 220, 60), rgb(90, 230, 90), (int)((hp - 0.5f) * 512))
                                   : lerp(rgb(255, 50, 30), rgb(240, 220, 60), (int)(hp * 512));
            if (blink) c = rgb(255, 255, 255);
            hline(hx, hx + 6, gy + gh - 1 - y, c);
        }
        rect(hx - 2, gy - 2, 10, gh + 4, blink ? rgb(255, 80, 60) : rgb(200, 220, 255));
        text(hx - 4, gy - 22, TR("ENG"), r.damage > 0.5f ? rgb(255, 140, 90) : rgb(150, 170, 200));
    }
}
