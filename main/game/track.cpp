// track.cpp - the three circuits, their scenery and backdrops.
#include "track.h"
#include "sprites.h"
#include "gfx.h"
#include <math.h>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using gfx::rgb;

const Theme g_themes[3] = {
    // Sunset Coast
    { "SUNSET COAST",
      rgb(34, 18, 84), rgb(200, 70, 120), rgb(255, 180, 96),
      rgb(246, 150, 120), 5.0f,
      rgb(98, 164, 64), rgb(86, 150, 58), rgb(226, 40, 46), rgb(246, 246, 246),
      rgb(108, 104, 116), rgb(100, 96, 108), rgb(246, 246, 236), rgb(196, 176, 140),
      rgb(130, 60, 130), rgb(176, 90, 150), rgb(84, 36, 92), rgb(110, 56, 110), rgb(150, 80, 120),
      256 },
    // Alpine Pass
    { "ALPINE PASS",
      rgb(28, 90, 210), rgb(90, 160, 240), rgb(196, 228, 255),
      rgb(214, 232, 250), 4.0f,
      rgb(76, 156, 74), rgb(66, 140, 66), rgb(210, 34, 34), rgb(244, 244, 244),
      rgb(110, 110, 118), rgb(102, 102, 110), rgb(244, 244, 244), rgb(150, 150, 150),
      rgb(110, 132, 176), rgb(240, 246, 255), rgb(36, 92, 64), rgb(52, 116, 78), rgb(150, 180, 170),
      256 },
    // Neon City (night)
    { "NEON CITY",
      rgb(4, 4, 20), rgb(26, 10, 60), rgb(86, 26, 110),
      rgb(44, 18, 74), 6.0f,
      rgb(26, 40, 46), rgb(22, 34, 40), rgb(255, 40, 200), rgb(0, 210, 255),
      rgb(46, 46, 62), rgb(40, 40, 56), rgb(255, 226, 110), rgb(70, 70, 90),
      rgb(26, 16, 52), rgb(255, 210, 110), rgb(14, 10, 32), rgb(120, 60, 200), rgb(30, 14, 50),
      170 },
};

namespace {

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};

// Road lengths / curves / hills, same vocabulary as classic pseudo-3D racers
enum { NONE = 0, SHORT = 25, MEDIUM = 50, LONG = 100 };
enum { C_EASY = 2, C_MED = 4, C_HARD = 6 };
enum { H_LOW = 20, H_MED = 40, H_HIGH = 60 };

struct Builder {
    Track& t;
    explicit Builder(Track& tr) : t(tr) {}
    float lastY() const { return t.segs.empty() ? 0.0f : t.segs.back().y2; }
    void seg(float curve, float y) {
        Seg s;
        s.curve = curve;
        s.y1 = lastY();
        s.y2 = y;
        s.propFirst = 0; s.propCount = 0; s.flags = 0;
        t.segs.push_back(s);
    }
    static float easeIn(float a, float b, float p) { return a + (b - a) * p * p; }
    static float easeInOut(float a, float b, float p) { return a + (b - a) * ((-cosf(p * 3.14159265f) / 2) + 0.5f); }
    void road(int enter, int hold, int leave, float curve, float hill) {
        float startY = lastY(), endY = startY + hill * SEG_LEN;
        int total = enter + hold + leave;
        for (int n = 0; n < enter; ++n) seg(easeIn(0, curve, (float)n / enter), easeInOut(startY, endY, (float)n / total));
        for (int n = 0; n < hold; ++n)  seg(curve, easeInOut(startY, endY, (float)(enter + n) / total));
        for (int n = 0; n < leave; ++n) seg(easeInOut(curve, 0, (float)n / leave), easeInOut(startY, endY, (float)(enter + hold + n) / total));
    }
    void straight(int n, float hill = 0) { road(n, n, n, 0, hill); }
    void curve(int n, float c, float hill = 0) { road(n, n, n, c, hill); }
    void sCurves(float c) {
        road(MEDIUM, MEDIUM, MEDIUM, -c / 2, 0);
        road(MEDIUM, MEDIUM, MEDIUM, c, H_LOW);
        road(MEDIUM, MEDIUM, MEDIUM, c / 2, -H_LOW);
        road(MEDIUM, MEDIUM, MEDIUM, -c, 0);
        road(MEDIUM, MEDIUM, MEDIUM, -c / 2, 0);
    }
    void bumps() {
        road(10, 10, 10, 0, 5); road(10, 10, 10, 0, -2); road(10, 10, 10, 0, -5);
        road(10, 10, 10, 0, 8); road(10, 10, 10, 0, 5);  road(10, 10, 10, 0, -7);
        road(10, 10, 10, 0, 5); road(10, 10, 10, 0, -2);
    }
    void downhillToEnd(int n) { road(n, n, n, -C_EASY, -lastY() / SEG_LEN); }
};

// ---------------------------------------------------------------------------
// Script parser. One command per line (or separated by ';'), '#' starts a comment.
//   straight LEN [HILL]                  straight, optionally climbing/descending
//   curve    LEN CURVE [HILL]            a bend (entry, hold and exit each last LEN)
//   road     ENTER HOLD LEAVE CURVE [HILL]   the full-control primitive
//   scurves  CURVE                       ready-made S bend series (5 bends)
//   bumps                                ready-made series of small crests
//   downhill LEN                         smooth descent back to ground level
// Numbers can be written as a name (SHORT, MEDIUM, EASY, HARD, LOW...) or a value;
// a leading '-' / '+' sets the sign (-HARD = hard bend to the other side).
// ---------------------------------------------------------------------------
enum ArgKind { K_LEN, K_CURVE, K_HILL };

bool ieq(const char* a, int n, const char* b) {
    for (int i = 0; i < n; ++i) {
        char x = a[i]; if (x >= 'a' && x <= 'z') x -= 32;
        if (x != b[i]) return false;
    }
    return b[n] == 0;
}

bool parseValue(const char* s, int n, ArgKind kind, float& out) {
    float sign = 1;
    if (n > 0 && (s[0] == '-' || s[0] == '+')) { if (s[0] == '-') sign = -1; ++s; --n; }
    if (n <= 0) return false;
    static const struct { const char* name; float v; } kLen[] = {
        {"NONE", NONE}, {"SHORT", SHORT}, {"MEDIUM", MEDIUM}, {"MED", MEDIUM}, {"LONG", LONG} };
    static const struct { const char* name; float v; } kCurve[] = {
        {"EASY", C_EASY}, {"MED", C_MED}, {"MEDIUM", C_MED}, {"HARD", C_HARD} };
    static const struct { const char* name; float v; } kHill[] = {
        {"LOW", H_LOW}, {"MED", H_MED}, {"MEDIUM", H_MED}, {"HIGH", H_HIGH} };
    auto look = [&](const auto& tbl, int cnt) {
        for (int i = 0; i < cnt; ++i) if (ieq(s, n, tbl[i].name)) { out = sign * tbl[i].v; return true; }
        return false;
    };
    if (kind == K_LEN && look(kLen, 5)) return true;
    if (kind == K_CURVE && look(kCurve, 4)) return true;
    if (kind == K_HILL && look(kHill, 4)) return true;
    char buf[24];
    if (n >= (int)sizeof buf) return false;
    memcpy(buf, s, n); buf[n] = 0;
    char* endp = nullptr;
    float v = strtof(buf, &endp);
    if (endp == buf || *endp) return false;
    out = sign * v;
    return true;
}

struct Parser {
    Builder& b;
    char* err; int errSize; bool ok = true; int line = 1;
    Parser(Builder& bb, char* e, int es) : b(bb), err(e), errSize(es) {}

    void fail(const char* msg) {
        if (ok && err && errSize > 0) snprintf(err, errSize, "line %d: %s", line, msg);
        ok = false;
    }
    bool room() const { return b.t.count() < TRACK_MAX_SEGS; }

    void command(const char* s, int n) {
        const char* tok[8]; int len[8]; int nt = 0;
        int i = 0;
        while (i < n && nt < 8) {
            while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == '\r')) ++i;
            if (i >= n) break;
            int j = i;
            while (j < n && s[j] != ' ' && s[j] != '\t' && s[j] != ',' && s[j] != '\r') ++j;
            tok[nt] = s + i; len[nt] = j - i; ++nt; i = j;
        }
        if (nt == 0) return;
        auto arg = [&](int k, ArgKind kind, float& v, bool optional = false, float def = 0) {
            if (k >= nt) { if (optional) { v = def; return true; } fail("missing value"); return false; }
            if (!parseValue(tok[k], len[k], kind, v)) { fail("bad value"); return false; }
            return true;
        };
        auto lenArg = [&](int k, int& v, bool optional = false, int def = 0) {
            float f; if (!arg(k, K_LEN, f, optional, (float)def)) return false;
            if (f < 0 || f > 400) { fail("length must be 0..400 segments"); return false; }
            v = (int)f; return true;
        };
        auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
        if (!room()) { fail("track too long (max 3600 segments)"); return; }
        float c, h; int a, bb, d;
        if (ieq(tok[0], len[0], "STRAIGHT")) {
            if (!lenArg(1, a) || !arg(2, K_HILL, h, true, 0)) return;
            b.straight(a, clampf(h, -200, 200));
        } else if (ieq(tok[0], len[0], "CURVE")) {
            if (!lenArg(1, a) || !arg(2, K_CURVE, c) || !arg(3, K_HILL, h, true, 0)) return;
            b.curve(a, clampf(c, -12, 12), clampf(h, -200, 200));
        } else if (ieq(tok[0], len[0], "ROAD")) {
            if (!lenArg(1, a) || !lenArg(2, bb) || !lenArg(3, d) || !arg(4, K_CURVE, c) || !arg(5, K_HILL, h, true, 0)) return;
            b.road(a, bb, d, clampf(c, -12, 12), clampf(h, -200, 200));
        } else if (ieq(tok[0], len[0], "SCURVES")) {
            if (!arg(1, K_CURVE, c)) return;
            b.sCurves(clampf(c, -12, 12));
        } else if (ieq(tok[0], len[0], "BUMPS")) {
            b.bumps();
        } else if (ieq(tok[0], len[0], "DOWNHILL")) {
            if (!lenArg(1, a)) return;
            b.downhillToEnd(a);
        } else {
            fail("unknown command");
        }
    }
    void run(const char* script) {
        const char* p = script;
        while (*p) {
            const char* e = p;
            while (*e && *e != '\n' && *e != ';') ++e;
            int n = (int)(e - p);
            for (int k = 0; k < n; ++k) if (p[k] == '#') { n = k; break; }
            command(p, n);
            if (*e == '\n') ++line;
            p = *e ? e + 1 : e;
        }
        // keep the lap seamless: come back to ground level before the start line.
        // (road() stops a hair short of its target height, so ignore a few units.)
        if (b.t.count() > 0 && fabsf(b.lastY()) > 20.0f) {
            b.downhillToEnd(MEDIUM);
        }
    }
};

void addProp(std::vector<Prop>& v, int seg, int spr, float off, bool flip = false) {
    Prop p;
    p.seg = (uint16_t)seg; p.spr = (uint8_t)spr; p.offset = off; p.flip = flip ? 1 : 0;
    v.push_back(p);
}

void scenery(Track& t, int id) {
    Rng r(g_trackDefs[id].seed);
    auto& P = t.props;
    int n = t.count();
    // the starting gantry
    addProp(P, 4, SPR_GANTRY, 0.0f);

    // chevrons on the outside of sharp curves
    for (int i = 10; i < n - 5; i += 5) {
        float c = t.segs[i].curve;
        if (fabsf(c) >= 3.5f) addProp(P, i, SPR_CHEVRON, c > 0 ? -1.15f : 1.15f, c < 0);
    }

    auto side = [&]() { return r.f() < 0.5f ? -1.0f : 1.0f; };
    for (int i = 12; i < n - 4; ++i) {
        bool nearStart = i < 20;
        if (nearStart) continue;
        switch (t.theme) {
        case THEME_COAST:
            if (i % 6 == 0) addProp(P, i, SPR_PALM, -1.35f - r.f() * 0.5f, r.f() < 0.5f);
            if (i % 6 == 3) addProp(P, i, SPR_PALM, 1.35f + r.f() * 0.5f, r.f() < 0.5f);
            if (i % 9 == 0) addProp(P, i, SPR_BUSH, side() * r.range(2.0f, 4.5f));
            if (i % 31 == 0) addProp(P, i, SPR_ROCK, side() * r.range(2.4f, 5.0f));
            if (i % 120 == 60) addProp(P, i, (i / 120) % 2 ? SPR_BOARD_A : SPR_BOARD_B, side() * 1.6f);
            if (i % 200 == 100) addProp(P, i, SPR_BOARD_C, side() * 1.6f);
            break;
        case THEME_ALPINE:
            if (i % 4 == 0) addProp(P, i, SPR_PINE, -r.range(1.3f, 2.2f));
            if (i % 4 == 2) addProp(P, i, SPR_PINE, r.range(1.3f, 2.2f));
            if (i % 7 == 0) addProp(P, i, SPR_PINE, side() * r.range(2.5f, 6.0f));
            if (i % 13 == 0) addProp(P, i, SPR_ROCK, side() * r.range(1.6f, 4.0f));
            if (i % 17 == 0) addProp(P, i, SPR_TREE, side() * r.range(2.6f, 5.0f));
            if (i % 11 == 5) addProp(P, i, SPR_BUSH, side() * r.range(1.4f, 3.0f));
            if (i % 180 == 90) addProp(P, i, SPR_BOARD_B, side() * 1.6f);
            break;
        case THEME_CITY:
            if (i % 8 == 0) { addProp(P, i, SPR_LAMP, -1.12f, true); addProp(P, i, SPR_LAMP, 1.12f, false); }
            if (i % 5 == 0) addProp(P, i, SPR_BUILDING_A + r.irange(0, 2), -r.range(1.9f, 2.4f));
            if (i % 5 == 2) addProp(P, i, SPR_BUILDING_A + r.irange(0, 2), r.range(1.9f, 2.4f));
            if (i % 90 == 45) addProp(P, i, SPR_BOARD_A + r.irange(0, 2), side() * 1.5f);
            if (i % 23 == 0) addProp(P, i, SPR_TREE, side() * 1.45f);
            break;
        }
    }
    std::stable_sort(P.begin(), P.end(), [](const Prop& a, const Prop& b) { return a.seg < b.seg; });
    for (size_t k = 0; k < P.size(); ++k) {
        Seg& s = t.segs[P[k].seg];
        if (s.propCount == 0) s.propFirst = (uint16_t)k;
        if (s.propCount < 255) s.propCount++;
    }
}

void minimap(Track& t) {
    int n = t.count();
    float total = 0;
    for (auto& s : t.segs) total += s.curve;
    float k = fabsf(total) > 1.0f ? 6.2831853f / total : 0.003f;
    std::vector<float> xs(n + 1), ys(n + 1);
    float h = 0, x = 0, y = 0;
    for (int i = 0; i < n; ++i) {
        xs[i] = x; ys[i] = y;
        h += t.segs[i].curve * k;
        x += sinf(h); y -= cosf(h);
    }
    xs[n] = x; ys[n] = y;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i <= n; ++i) {   // close the loop by spreading the error
        xs[i] -= xs[n] * i / n; ys[i] -= ys[n] * i / n;
        minx = fminf(minx, xs[i]); maxx = fmaxf(maxx, xs[i]);
        miny = fminf(miny, ys[i]); maxy = fmaxf(maxy, ys[i]);
    }
    float sc = 54.0f / fmaxf(maxx - minx, maxy - miny);
    float cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    for (int m = 0; m < MAP_N; ++m) {
        int i = m * n / MAP_N;
        t.mapX[m] = (int8_t)lroundf((xs[i] - cx) * sc);
        t.mapY[m] = (int8_t)lroundf((ys[i] - cy) * sc);
    }
}

// fractal 1D noise for the mountain silhouettes
float noise1(Rng& r, float* tbl, int n, float x) {
    (void)r;
    int i = (int)x; float f = x - i;
    float a = tbl[i % n], b = tbl[(i + 1) % n];
    f = f * f * (3 - 2 * f);
    return a + (b - a) * f;
}

void backdrop(Track& t) {
    Rng r(g_trackDefs[t.id].bgSeed);
    const int W = Track::BG_W;
    float tbl[64];
    for (float& v : tbl) v = r.f();
    for (int x = 0; x < W; ++x) {
        float u = (float)x / W * 64;
        float fh = 0;
        if (t.theme == THEME_CITY) {
            // skyline: blocky
            int b = x / 18;
            Rng br(b * 7919 + 3);
            fh = 18 + br.f() * 52;
            if (br.f() < 0.2f) fh += 30;
        } else {
            fh = noise1(r, tbl, 64, u * 0.25f) * 0.55f + noise1(r, tbl, 64, u * 0.6f + 13) * 0.3f + noise1(r, tbl, 64, u * 2.0f + 7) * 0.15f;
            fh = t.theme == THEME_ALPINE ? 14 + fh * fh * 110 : 8 + fh * 52;
        }
        t.farH[x] = (uint8_t)fminf(fh, 120);
        float nh;
        if (t.theme == THEME_CITY) {
            int b = (x + 7) / 30;
            Rng br(b * 104729 + 11);
            nh = 6 + br.f() * 30;
        } else {
            nh = noise1(r, tbl, 64, u * 1.3f + 31) * 0.7f + noise1(r, tbl, 64, u * 5.0f + 3) * 0.3f;
            nh = 4 + nh * (t.theme == THEME_ALPINE ? 34 : 22);
            if (t.theme == THEME_ALPINE) nh += ((x * 13) % 7) * 0.6f;   // ragged forest edge
        }
        t.nearH[x] = (uint8_t)fminf(nh, 100);
    }
}

} // namespace

bool track_parse_script(Track& t, const char* script, char* err, int errSize) {
    if (err && errSize > 0) err[0] = 0;
    t.segs.clear();
    t.props.clear();
    Builder b(t);
    Parser p(b, err, errSize);
    p.run(script ? script : "");
    return p.ok;
}

void track_build(Track& t, int id) {
    if (id < 0 || id >= TRACK_COUNT) id = 0;
    const TrackDef& def = g_trackDefs[id];
    t.id = id;
    t.theme = def.theme;
    t.name = def.name;
    t.segs.reserve(2600);
    track_parse_script(t, def.script);
    if (t.segs.empty()) { Builder b(t); b.straight(MEDIUM); }   // never leave an empty road
    for (int i = 0; i < 3 && i < t.count(); ++i) t.segs[i].flags |= SEGF_START;
    t.length = t.count() * SEG_LEN;
    scenery(t, id);
    minimap(t);
    backdrop(t);
}
