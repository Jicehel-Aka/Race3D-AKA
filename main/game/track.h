// track.h - road segments, scenery props and visual themes.
#pragma once
#include <stdint.h>
#include <vector>

constexpr float SEG_LEN = 200.0f;     // world units per segment
constexpr float ROAD_W = 2000.0f;     // half width of the road
constexpr int   RUMBLE_LEN = 3;       // segments per rumble strip colour
constexpr int   LANES = 3;

// Looks: each one has its own sky, palette, backdrop, sprites and scenery recipe.
enum ThemeId { THEME_COAST = 0, THEME_ALPINE = 1, THEME_CITY = 2,
               THEME_TROPIC = 3, THEME_GLACIER = 4, THEME_HARBOR = 5, THEME_COUNT = 6 };

struct Theme {
    const char* name;
    uint16_t skyTop, skyMid, skyHorizon;
    uint16_t fog;
    float fogDensity;
    uint16_t grass1, grass2, rumble1, rumble2, road1, road2, lane, shoulder;
    uint16_t farCol, farHi, nearCol, nearHi, ground;
    int ambient;          // 256 = full daylight
    uint16_t alt1, alt2;  // alternate ground (sand, ice, water...) for segments flagged SEGF_ALT
};

struct Seg {
    float curve;
    float y1, y2;         // world height at start / end of the segment
    uint16_t propFirst;
    uint8_t propCount;
    uint8_t flags;        // SEGF_*
};
enum { SEGF_START = 1, SEGF_TUNNEL = 2, SEGF_ALT = 4 };

struct Prop {
    uint16_t seg;
    uint8_t spr;
    uint8_t flip;
    float offset;         // lateral position, road edge = +-1
};

constexpr int MAP_N = 128;

struct Track {
    int id = 0;
    const char* name = "";
    int theme = 0;
    std::vector<Seg> segs;
    std::vector<Prop> props;
    float length = 0;     // world length of one lap
    int8_t mapX[MAP_N], mapY[MAP_N];   // minimap polyline, centred on 0
    // parallax background height maps (BG_W columns, wrap around)
    static constexpr int BG_W = 1024;
    uint8_t farH[BG_W], nearH[BG_W];

    int count() const { return (int)segs.size(); }
    const Seg& at(float z) const {
        int n = count();
        int i = (int)(z / SEG_LEN) % n;
        if (i < 0) i += n;
        return segs[i];
    }
    int indexAt(float z) const {
        int n = count();
        int i = (int)floorf_(z / SEG_LEN) % n;
        return i < 0 ? i + n : i;
    }
    static float floorf_(float v) { int i = (int)v; return (float)(v < i ? i - 1 : i); }
};

extern const Theme g_themes[THEME_COUNT];

// ---- circuits ---------------------------------------------------------------
// A circuit is a small text script (see docs/CIRCUITS.md) plus a few settings.
// The list lives in tracks_data.cpp; tools/track_editor.html writes those entries.
struct TrackDef {
    const char* name;       // shown in the menu and on the HUD (<= 16 chars)
    int theme;              // ThemeId: sky, colours, scenery style
    int music;              // song index in music.h (0..SONG_COUNT-1)
    uint32_t seed;          // scenery random seed (trees, signs, rocks...)
    uint32_t bgSeed;        // backdrop (mountains / skyline) random seed
    const char* script;     // road layout, one command per line
};
constexpr int TRACK_COUNT = 6;
extern const TrackDef g_trackDefs[TRACK_COUNT];

void track_build(Track& t, int id);
// Builds only the road from a script (no scenery). Returns false and fills
// err (if not null) with a "line N: message" text when the script has a problem;
// the valid commands before/after a bad line are still applied.
bool track_parse_script(Track& t, const char* script, char* err = nullptr, int errSize = 0);
constexpr int TRACK_MAX_SEGS = 3600;
