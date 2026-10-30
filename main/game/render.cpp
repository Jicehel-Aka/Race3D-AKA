// render.cpp - the "OutRun style" segment renderer and everything on top.
#include "render.h"
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
    if (theme == THEME_CITY) {          // a few twinkling stars
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

void draw_layers(const Race& r, int baseY) {
    const Track& tr = r.track;
    const Theme& th = g_themes[tr.theme];
    const int M = Track::BG_W - 1;
    int off = (int)r.skyOffset, offN = (int)r.nearOffset;
    for (int x = 0; x < W; ++x) {
        int col = (x + off) & M;
        int h = tr.farH[col];
        int top = baseY - h;
        uint16_t* p = fb + x;
        for (int y = top < 0 ? 0 : top; y < baseY && y < H; ++y) {
            int alt = baseY - y;
            uint16_t c = s_farGrad[alt > 127 ? 127 : alt];
            if (tr.theme == THEME_ALPINE) {
                int snowLine = 58 + (int)(hash32(col) % 6);
                int slope = (int)tr.farH[(col + 3) & M] - (int)tr.farH[(col - 3) & M];   // > 0 : faces away from the sun
                if (alt > snowLine) c = (slope > 1 || ((col ^ y) & 7) == 0) ? lerp(th.farHi, th.farCol, 80) : th.farHi;
                else if (slope > 0) c = scale(c, 256 - (slope > 8 ? 8 : slope) * 7);
            } else if (tr.theme == THEME_CITY) {
                if (alt > 3 && alt < h - 2 && ((col / 2) & 1) == 0 && ((y + 1) % 4) == 0 && (hash32(col * 131 + y) % 5) == 0) c = th.farHi;
            } else if (alt > h - 3) {
                c = lerp(c, th.farHi, 160);
            }
            p[y * W] = c;
        }
    }
    for (int x = 0; x < W; ++x) {
        int col = (x + offN) & M;
        int h = tr.nearH[col];
        int top = baseY - h;
        uint16_t* p = fb + x;
        for (int y = top < 0 ? 0 : top; y < baseY + 2 && y < H; ++y) {
            int alt = baseY - y;
            if (alt < 0) alt = 0;
            uint16_t c = s_nearGrad[alt > 127 ? 127 : alt];
            if (tr.theme == THEME_CITY) {
                if (alt >= h - 1) c = th.nearHi;
                else if ((col % 5) == 1 && (y % 5) == 1 && (hash32(col * 7 + y) & 3) == 0) c = rgb(255, 200, 120);
            } else if (alt >= h - 1) c = th.nearHi;
            p[y * W] = c;
        }
    }
    for (int y = baseY + 2; y < H; ++y) hline(0, W, y, th.ground);
}

// ------------------------------------------------------------------ road
void draw_segment_rows(const PSeg& ps, int maxy, const Theme& th, const Seg& sg) {
    bool light = ((ps.idx / RUMBLE_LEN) & 1) == 0;
    int f = ps.fog;
    uint16_t grass = lerp(light ? th.grass1 : th.grass2, th.fog, f);
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
    if (r.track.theme == THEME_CITY && pr.spr == SPR_LAMP && dw > 3 && ps.fog < 200) {
        float hx = pr.flip ? (60 - 46) : 46;
        int gx = x + (int)(hx * destW / 60), gy = y + (int)(18 * destH / 180);
        if (gy < ps.clip) glowCircle(gx, gy, (int)(destW * 0.45f) + 1, scale(rgb(150, 120, 60), 256 - ps.fog));
    }
}

void draw_car(const Race& r, const Car& c, int x, int y, int dw, int dh, int clip, int fog, bool isPlayerView) {
    const Theme& th = g_themes[r.track.theme];
    int id = SPR_CAR;
    if (c.steerVis < -0.3f) id = SPR_CAR_LEFT;
    else if (c.steerVis > 0.3f) id = SPR_CAR_RIGHT;
    const Sprite& sp = g_sprites[id];
    uint16_t base[16];
    car_palette(base, c.livery, c.braking);
    int amb = th.ambient < 256 ? th.ambient + 40 : 256;
    const uint16_t* pal = lit_palette(base, sp.emissive, amb, th.fog, fog);
    blit(sp, pal, x, y, dw, dh, clip, false);
    if ((c.braking || th.ambient < 256) && dw > 8 && fog < 200) {
        uint16_t gc = c.braking ? rgb(140, 20, 20) : rgb(70, 10, 10);
        int ly = y + 31 * dh / 58;
        if (ly < clip) {
            int rad = dw / (isPlayerView ? 8 : 7) + 1;
            glowCircle(x + 20 * dw / 112, ly, rad, scale(gc, 256 - fog));
            glowCircle(x + 92 * dw / 112, ly, rad, scale(gc, 256 - fog));
        }
    }
}

} // namespace

// =================================================================== world
void render_world(const Race& r, float time) {
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
            int px = W / 2 - 56 + (int)(shakeX * 0.01f), py = H - 58 - 3 - (int)bounce;
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
struct Particle { float x, y, vx, vy, life, max; uint16_t c; uint8_t size; };
constexpr int MAXP = 120;
Particle s_p[MAXP];
int s_pn = 0;
uint32_t s_seed = 99;
float prand() { s_seed ^= s_seed << 13; s_seed ^= s_seed >> 17; s_seed ^= s_seed << 5; return (s_seed & 0xFFFF) / 65536.0f; }

void spawn(float x, float y, float vx, float vy, float life, uint16_t c, int size) {
    if (s_pn >= MAXP) return;
    s_p[s_pn++] = { x, y, vx, vy, life, life, c, (uint8_t)size };
}
} // namespace

void fx_clear() { s_pn = 0; }

void fx_burst(int x, int y, int n, uint16_t c, float speed, float life) {
    for (int i = 0; i < n; ++i) {
        float a = prand() * 6.2831f, v = speed * (0.3f + prand());
        spawn((float)x, (float)y, cosf(a) * v, sinf(a) * v - speed * 0.5f, life * (0.5f + prand()), c, 1 + (int)(prand() * 2));
    }
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
    for (int i = 0; i < s_pn;) {
        Particle& p = s_p[i];
        p.life -= dt;
        if (p.life <= 0) { s_p[i] = s_p[--s_pn]; continue; }
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.vy += 40 * dt;
        ++i;
    }
}

void fx_draw() {
    for (int i = 0; i < s_pn; ++i) {
        const Particle& p = s_p[i];
        int t = (int)(p.life / p.max * 256);
        int s = p.size + (int)((1.0f - p.life / p.max) * 3);
        blendRect((int)p.x - s / 2, (int)p.y - s / 2, s, s, p.c, t * 3 / 4);
    }
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
        text(x + 21, yy, c.name, c.isPlayer ? rgb(255, 230, 80) : rgb(240, 240, 240));
    }
}

static const char* ordinal(int p) {
    switch (p) { case 1: return "ST"; case 2: return "ND"; case 3: return "RD"; default: return "TH"; }
}

void render_hud(const Race& r, float time) {
    const Car& pl = r.player();
    char buf[32];

    // position
    snprintf(buf, sizeof buf, "%d", pl.place);
    textFancy(8, 6, buf, rgb(255, 246, 120), rgb(255, 120, 20), rgb(30, 10, 10), 4);
    textFancy(8 + 34, 8, ordinal(pl.place), rgb(255, 246, 120), rgb(255, 140, 30), rgb(30, 10, 10), 2);
    snprintf(buf, sizeof buf, "/%d", CAR_COUNT);
    textOutline(8 + 36, 26, buf, rgb(255, 255, 255), rgb(0, 0, 0));
    render_tower(r, 6, 44);

    // lap + times
    snprintf(buf, sizeof buf, "%d/%d", r.playerLap(), r.laps);
    int lw = textWidth(buf, 3);
    textOutline(W - 8 - lw - 28, 12, "LAP", rgb(255, 255, 255), rgb(0, 0, 0));
    textFancy(W - 8 - lw, 6, buf, rgb(255, 255, 255), rgb(140, 200, 255), rgb(10, 20, 40), 3);
    char t1[16], t2[16];
    format_time(t1, r.raceTime - pl.lapStart);
    snprintf(buf, sizeof buf, "TIME %s", t1);
    textOutline(W - 8 - textWidth(buf), 36, buf, rgb(255, 255, 255), rgb(0, 0, 0));
    if (pl.bestLap > 0) {
        format_time(t2, pl.bestLap);
        snprintf(buf, sizeof buf, "BEST %s", t2);
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
    text(cx - 16, cy + 26, "KM/H", rgb(180, 180, 200));
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
    text(gx - 9, gy - 12, "N2O", r.nitro > 0.99f ? rgb(120, 255, 255) : rgb(150, 170, 200));
}
