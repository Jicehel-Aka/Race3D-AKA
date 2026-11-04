// game.cpp - title screen, countdown, race flow, results and records.
#include "game.h"
#include "gfx.h"
#include "race.h"
#include "render.h"
#include "sprites.h"
#include "music.h"
#include "online.h"
#include "i18n.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace gfx;

namespace {

enum State { ST_TITLE, ST_COUNTDOWN, ST_RACING, ST_FINISHED, ST_RESULTS, ST_ONLINE, ST_NAME };

constexpr int NAME_MAX = 10;
void panel(int x, int y, int w, int h);
int fit_scale(const char* s, int scale) { while (scale > 1 && textWidth(s, scale) > W - 12) --scale; return scale; }
const char* player_name();
struct Records {
    uint32_t magic;
    float bestLap[TRACK_COUNT];
    float bestRace[TRACK_COUNT][DIFF_COUNT];
    uint8_t bestPlace[TRACK_COUNT][DIFF_COUNT];
    uint8_t musicOff;
    char name[NAME_MAX + 2];      // player name (online races), kept between sessions
};
constexpr uint32_t REC_MAGIC = 0x52443304;

// Records file of version 1.1 (no player name): still read.
struct RecordsV3 {
    uint32_t magic;
    float bestLap[TRACK_COUNT];
    float bestRace[TRACK_COUNT][DIFF_COUNT];
    uint8_t bestPlace[TRACK_COUNT][DIFF_COUNT];
    uint8_t musicOff;
};
constexpr uint32_t REC_MAGIC_V3 = 0x52443303;

// Records file of version 1.0 (3 circuits): still read so players keep their times.
struct RecordsV1 {
    uint32_t magic;
    float bestLap[3];
    float bestRace[3][DIFF_COUNT];
    uint8_t bestPlace[3][DIFF_COUNT];
    uint8_t musicOff;
};
constexpr uint32_t REC_MAGIC_V1 = 0x52443302;

Race g_race;
Records g_rec;
State g_state = ST_TITLE;
float g_time = 0;          // global animation clock
float g_stateT = 0;        // time in current state
bool g_paused = false;
int g_pauseSel = 0;

int g_menuSel = 6;
constexpr int MENU_ITEMS = 8;   // track, difficulty, laps, music, language, player name, start, online
constexpr int MI_LANG = 4, MI_NAME = 5, MI_START = 6, MI_ONLINE = 7;
volatile bool g_titleRequest = false;
int g_track = 0, g_diff = DIFF_NORMAL, g_lapsSel = 2;
const int kLaps[] = { 1, 2, 3, 5 };

char g_msg[40] = "";
float g_msgT = 0;
uint16_t g_msgTop = 0, g_msgBot = 0;
bool g_newLapRecord = false, g_newRaceRecord = false;
int g_countBeep = 0;

// online races
bool g_online = false;            // a connection is open (lobby / race)
bool g_onlineRace = false;        // the current race is an online one
bool g_cfgSent = false, g_lost = false;
int g_loadRes = 0;
online::Settings g_set;
float g_telemT = 0;
int g_lastPhase = 0;

const char* const kDiffDesc[DIFF_COUNT] = {
    "Relaxed rivals who make mistakes",
    "A balanced field, a fair fight",
    "Fast and sharp. Use your nitro!",
    "Pro drivers. Perfect lines only.",
};

void message(const char* s, uint16_t top, uint16_t bot, float dur = 1.6f) {
    strncpy(g_msg, TR(s), sizeof g_msg - 1);
    g_msg[sizeof g_msg - 1] = 0;
    g_msgTop = top; g_msgBot = bot; g_msgT = dur;
}

void load_records() {
    if (plat::loadBlob(&g_rec, sizeof g_rec) && g_rec.magic == REC_MAGIC) { g_rec.name[NAME_MAX] = 0; return; }
    RecordsV3 v3;
    if (plat::loadBlob(&v3, sizeof v3) && v3.magic == REC_MAGIC_V3) {
        memset(&g_rec, 0, sizeof g_rec);
        g_rec.magic = REC_MAGIC;
        memcpy(&g_rec.bestLap, &v3.bestLap, sizeof v3 - sizeof(uint32_t));
        return;
    }
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.magic = REC_MAGIC;
    RecordsV1 old;
    if (plat::loadBlob(&old, sizeof old) && old.magic == REC_MAGIC_V1) {   // upgrade a 3-circuit file
        for (int t = 0; t < 3; ++t) {
            g_rec.bestLap[t] = old.bestLap[t];
            for (int d = 0; d < DIFF_COUNT; ++d) {
                g_rec.bestRace[t][d] = old.bestRace[t][d];
                g_rec.bestPlace[t][d] = old.bestPlace[t][d];
            }
        }
        g_rec.musicOff = old.musicOff;
    }
}

void apply_music_volume() { plat::setMusicVolume(g_rec.musicOff ? 0.0f : 0.8f); }

void start_demo() {
    g_race.playerIdx = CAR_COUNT - 1;
    plat::playMusic(g_trackDefs[g_track].music);
    g_race.setup(g_track, DIFF_HARD, 99, true);
    g_race.cameraZ();
    // spread the field so the attract mode shows some traffic
    for (int i = 0; i < CAR_COUNT; ++i) g_race.cars[i].dist += SEG_LEN * 40;
    fx_clear();
}

void leave_online() {
    if (g_online) online::end();
    g_online = false; g_onlineRace = false;
}

void go_title() {
    leave_online();
    g_state = ST_TITLE; g_stateT = 0; g_paused = false;
    start_demo();
}

void start_race() {
    g_onlineRace = false;
    g_race.playerIdx = CAR_COUNT - 1;
    g_race.setup(g_track, g_diff, kLaps[g_lapsSel], false);
    g_race.cars[g_race.playerIdx].name = player_name();
    plat::playMusic(g_trackDefs[g_track].music, true);
    fx_clear();
    g_state = ST_COUNTDOWN; g_stateT = 0;
    g_countBeep = 0; g_msgT = 0;
    g_newLapRecord = g_newRaceRecord = false;
    plat::playSfx(plat::SFX_MENU_OK);
}

void finish_race() {
    g_race.estimateUnfinished();
    const Car& p = g_race.player();
    if (g_onlineRace) { g_newLapRecord = g_newRaceRecord = false; g_state = ST_RESULTS; g_stateT = 0; return; }
    if (!p.retired && p.bestLap > 0 && (g_rec.bestLap[g_track] == 0 || p.bestLap < g_rec.bestLap[g_track])) {
        g_rec.bestLap[g_track] = p.bestLap; g_newLapRecord = true;
    }
    float& br = g_rec.bestRace[g_track][g_diff];
    if (!p.retired && g_race.laps == 3 && (br == 0 || p.finishTime < br)) { br = p.finishTime; g_newRaceRecord = true; }
    uint8_t& bp = g_rec.bestPlace[g_track][g_diff];
    if (bp == 0 || p.place < bp) bp = (uint8_t)p.place;
    plat::saveBlob(&g_rec, sizeof g_rec);
    g_state = ST_RESULTS; g_stateT = 0;
}

void start_online_race() {
    const online::Info& in = online::info();
    g_track = in.track % TRACK_COUNT;
    for (int i = 0; i < 4; ++i) if (kLaps[i] == in.laps) g_lapsSel = i;
    g_race.playerIdx = in.slot;
    g_race.setup(g_track, g_diff, in.laps, false);
    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = g_race.cars[i];
        if (c.isPlayer) continue;
        c.remote = true;
        c.netDist = c.dist; c.netX = c.x; c.netSpeed = 0; c.netSteer = 0; c.netAge = 0;
    }
    plat::playMusic(g_trackDefs[g_track].music, true);
    fx_clear();
    g_onlineRace = true; g_lost = false; g_telemT = 0; g_paused = false;
    g_state = ST_COUNTDOWN; g_stateT = 0;
    g_countBeep = 0; g_msgT = 0;
    g_newLapRecord = g_newRaceRecord = false;
    plat::playSfx(plat::SFX_MENU_OK);
}

// ------------------------------------------------------------ player name editor (on-screen keyboard)
char g_edit[NAME_MAX + 2] = "";
int g_editLen = 0, g_kx = 0, g_ky = 0;
const char* const kKeys[4] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-_" };

void open_name_editor() {
    strncpy(g_edit, g_rec.name, NAME_MAX); g_edit[NAME_MAX] = 0;
    g_editLen = (int)strlen(g_edit);
    g_kx = 0; g_ky = 0;
    g_state = ST_NAME; g_stateT = 0;
    plat::playSfx(plat::SFX_MENU_OK);
}

void close_name_editor(bool save) {
    if (save && g_editLen > 0) {
        memcpy(g_rec.name, g_edit, sizeof g_rec.name);
        plat::saveBlob(&g_rec, sizeof g_rec);
    }
    g_state = ST_TITLE; g_stateT = 0;
    plat::playSfx(plat::SFX_MENU_OK);
}

void update_name_editor(const plat::Input& inRaw, float dt) {
    plat::Input in = inRaw;
    static float hold = 0;                                   // key repeat: faster travel over the grid
    uint32_t dir = in.held & (plat::BTN_LEFT | plat::BTN_RIGHT | plat::BTN_UP | plat::BTN_DOWN);
    if (dir && !(in.pressed & dir)) { hold += dt; if (hold > 0.4f) { hold -= 0.09f; in.pressed |= dir; } }
    else hold = 0;
    if (in.pressed & plat::BTN_LEFT) g_kx = (g_kx + 9) % 10;
    if (in.pressed & plat::BTN_RIGHT) g_kx = (g_kx + 1) % 10;
    if (in.pressed & plat::BTN_UP) g_ky = (g_ky + 3) % 4;
    if (in.pressed & plat::BTN_DOWN) g_ky = (g_ky + 1) % 4;
    if (in.pressed & (plat::BTN_LEFT | plat::BTN_RIGHT | plat::BTN_UP | plat::BTN_DOWN)) plat::playSfx(plat::SFX_MENU_MOVE);
    bool del = (g_ky == 3 && g_kx == 8), ok = (g_ky == 3 && g_kx == 9);
    if (in.pressed & plat::BTN_A) {
        if (ok) { close_name_editor(true); return; }
        if (del) { if (g_editLen > 0) g_edit[--g_editLen] = 0; }
        else if (g_editLen < NAME_MAX) { g_edit[g_editLen++] = kKeys[g_ky][g_kx]; g_edit[g_editLen] = 0; }
        plat::playSfx(plat::SFX_MENU_MOVE);
    }
    if ((in.pressed & plat::BTN_R1) && g_editLen < NAME_MAX && g_editLen > 0) { g_edit[g_editLen++] = ' '; g_edit[g_editLen] = 0; }   // R1: space
    if (((in.pressed & plat::BTN_B) || (in.pressed & plat::BTN_L1)) && g_editLen > 0) { g_edit[--g_editLen] = 0; plat::playSfx(plat::SFX_MENU_MOVE); }
    if (in.pressed & plat::BTN_RUN) close_name_editor(true);
}

void draw_name_editor() {
    render_world(g_race, g_time);
    darkRect(0, 0, W, H);
    const char* title = TR("PLAYER NAME");
    { int sc = fit_scale(title, 2); textFancy(W / 2 - textWidth(title, sc) / 2, 14, title, rgb(170, 230, 255), rgb(60, 130, 255), rgb(0, 0, 30), sc); }
    panel(40, 44, W - 80, 28);
    char shown[NAME_MAX + 4];
    snprintf(shown, sizeof shown, "%s%s", g_edit, ((int)(g_time * 3) & 1) && g_editLen < NAME_MAX ? "_" : "");
    textOutline(W / 2 - textWidth(shown, 2) / 2, 52, shown, rgb(255, 240, 120), rgb(0, 0, 0), 2);
    panel(40, 84, W - 80, 104);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 10; ++c) {
            int x = 52 + c * 22, y = 94 + r * 22;
            bool sel = g_kx == c && g_ky == r;
            char label[8]; label[0] = 0;
            if (r < 3 || c < 8) { label[0] = kKeys[r][c]; label[1] = 0; }
            else snprintf(label, sizeof label, "%s", c == 8 ? "<" : "OK");
            if (sel) blendRect(x - 2, y - 3, 20, 18, rgb(255, 200, 40), 130);
            else rect(x - 2, y - 3, 20, 18, rgb(70, 70, 110));
            text(x + 8 - textWidth(label) / 2, y + 2, label, sel ? rgb(255, 255, 255) : rgb(190, 200, 230));
        }
    }
    const char* h = TR("A: ADD   B: DELETE   RUN: OK");
    text(W / 2 - textWidth(h) / 2, 204, h, rgb(200, 200, 230));
}

void enter_online_lobby() {
    g_onlineRace = false;
    g_state = ST_ONLINE; g_stateT = 0; g_paused = false;
    g_lastPhase = online::info().phase;
    start_demo();
}

void go_online() {
    g_loadRes = online::loadSettings(g_set);
    if (!g_rec.name[0] && g_set.name[0]) { snprintf(g_rec.name, sizeof g_rec.name, "%s", g_set.name); plat::saveBlob(&g_rec, sizeof g_rec); }
    snprintf(g_set.name, sizeof g_set.name, "%s", player_name());     // the name chosen in the game wins over the file
    g_cfgSent = false;
    if (g_loadRes == online::LOAD_OK) { online::begin(g_set); g_online = true; }
    g_state = ST_ONLINE; g_stateT = 0; g_lastPhase = 0;
    plat::playSfx(plat::SFX_MENU_OK);
}

// when the link drops during a race the other cars go back to the local AI
void online_lost(bool dq = false) {
    if (g_lost) return;
    g_lost = true;
    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = g_race.cars[i];
        if (c.remote) { c.remote = false; c.targetX = c.x; c.retired = false; }
    }
    if (dq) message("DISQUALIFIED", rgb(255, 90, 70), rgb(120, 10, 10), 4.0f);
    else message("CONNECTION LOST", rgb(255, 120, 100), rgb(160, 20, 10), 3.0f);
}

// online per-frame work during a race: read the network, follow the other cars, send ours at 20 Hz
void online_tick(float dt) {
    online::poll();
    if (online::kicked() && !g_lost) { online_lost(true); return; }
    online::applyNames(g_race);
    char who[12];
    if (online::takeLeft(who, sizeof who)) {
        char m[40]; snprintf(m, sizeof m, "%s %s", who, TR("LEFT THE RACE"));
        message(m, rgb(255, 210, 120), rgb(150, 70, 10), 2.5f);
    }
    if (!g_lost && !online::isOnline()) { online_lost(); return; }
    if (g_lost) return;
    online::feed(g_race);
    g_telemT += dt;
    if (g_telemT >= 0.05f) { g_telemT = fmodf(g_telemT, 0.05f); online::sendTelemetry(g_race); }
}

// ------------------------------------------------------------ drawing helpers
void panel(int x, int y, int w, int h) {
    blendRect(x, y, w, h, rgb(8, 6, 24), 190);
    hline(x, x + w, y, rgb(255, 255, 255));
    hline(x, x + w, y + h - 1, rgb(120, 120, 160));
    for (int j = 0; j < h; ++j) { pset(x, y + j, rgb(200, 200, 230)); pset(x + w - 1, y + j, rgb(120, 120, 160)); }
}

void draw_logo(int y) {
    const char* l1 = "AKA TURBO";
    int w1 = textWidth(l1, 4);
    float wob = sinf(g_time * 2.0f) * 2;
    textFancy(W / 2 - w1 / 2, y + (int)wob, l1, rgb(255, 250, 150), rgb(255, 60, 40), rgb(40, 0, 20), 4);
    const char* l2 = "3D  GRAND  PRIX";
    textFancy(W / 2 - textWidth(l2, 2) / 2, y + 38, l2, rgb(160, 255, 255), rgb(40, 140, 255), rgb(0, 10, 40), 2);
    // moving glint
    int gx = (int)(fmodf(g_time * 140, W + 200)) - 100;
    for (int yy = y - 2; yy < y + 34; ++yy) {
        int xx = gx + (yy - y);
        for (int k = 0; k < 6; ++k) {
            int px = xx + k;
            if ((unsigned)px >= (unsigned)W || (unsigned)yy >= (unsigned)H) continue;
            uint16_t c = fb[yy * W + px];
            int r, g, b; unpack(c, r, g, b);
            if (r > 200 && g > 40) fb[yy * W + px] = lerp(c, 0xFFFF, 150);
        }
    }
}

const char* player_name() { return g_rec.name[0] ? g_rec.name : "PLAYER"; }

void draw_title() {
    render_world(g_race, g_time);
    // vignette at the top for the logo
    for (int y = 0; y < 70; ++y) blendRect(0, y, W, 1, rgb(0, 0, 0), (70 - y) * 2);
    draw_logo(10);

    const int px = 16, py = 68, pw = W - 32, ph = 154;
    panel(px, py, pw, ph);
    char buf[48];
    const char* labels[6] = { "TRACK", "DIFFICULTY", "LAPS", "MUSIC", "LANGUAGE", "PLAYER" };
    for (int i = 0; i < 6; ++i) {
        int y = py + 7 + i * 14;
        bool sel = g_menuSel == i;
        if (sel) blendRect(px + 4, y - 3, pw - 8, 13, rgb(255, 200, 40), 80);
        text(px + 10, y, TR(labels[i]), sel ? rgb(255, 230, 90) : rgb(170, 170, 200));
        const char* v = i == 0 ? g_trackDefs[g_track].name : (i == 1 ? TR(g_diffNames[g_diff]) : nullptr);
        if (i == 2) { snprintf(buf, sizeof buf, "%d", kLaps[g_lapsSel]); v = buf; }
        if (i == 3) v = g_rec.musicOff ? TR("OFF") : g_songs[g_trackDefs[g_track].music].title;
        if (i == 4) v = i18n::nativeName(i18n::language());
        if (i == 5) v = player_name();
        int vx = px + 124;
        if (sel) {
            bool blink = ((int)(g_time * 4) & 1);
            if (i != 5) text(vx - 14 - (blink ? 1 : 0), y, "<", rgb(255, 255, 255));
            if (i != 5) text(vx + 132 + (blink ? 1 : 0), y, ">", rgb(255, 255, 255));
            else text(vx + 132, y, "A", rgb(255, 255, 255));
        }
        uint16_t vc = rgb(255, 255, 255);
        if (i == 3 && g_rec.musicOff) vc = rgb(150, 150, 170);
        if (i == 1) { const uint16_t dc[] = { rgb(120, 255, 140), rgb(140, 210, 255), rgb(255, 190, 70), rgb(255, 80, 80) }; vc = dc[g_diff]; }
        if (i == 5) vc = rgb(140, 230, 255);
        textOutline(vx + 66 - textWidth(v) / 2, y, v, vc, rgb(0, 0, 0));
    }
    text(px + 10, py + 92, TR(kDiffDesc[g_diff]), rgb(150, 150, 180));

    // records for the selection
    char t[16];
    if (g_rec.bestLap[g_track] > 0) { format_time(t, g_rec.bestLap[g_track]); snprintf(buf, sizeof buf, "%s %s", TR("LAP RECORD"), t); }
    else snprintf(buf, sizeof buf, "%s --'--\"--", TR("LAP RECORD"));
    text(px + 10, py + 103, buf, rgb(255, 220, 110));
    uint8_t bp = g_rec.bestPlace[g_track][g_diff];
    if (bp) { snprintf(buf, sizeof buf, "%s: P%d", TR("BEST"), bp); text(px + pw - 10 - textWidth(buf), py + 103, buf, rgb(255, 220, 110)); }

    bool sel = g_menuSel == MI_START;
    const char* go = TR("START RACE");
    int gs = textWidth(go, 2) <= pw - 40 ? 2 : 1;
    int k = sel ? (int)(sinf(g_time * 6) * 60 + 196) : 140;
    int sw = textWidth(go, gs);
    if (sel) blendRect(W / 2 - sw / 2 - 10, py + 114, sw + 20, gs == 2 ? 20 : 12, rgb(255, 60, 40), 120);
    textFancy(W / 2 - sw / 2, py + 116, go, scale(rgb(255, 255, 255), k), scale(rgb(255, 210, 60), k), rgb(40, 0, 0), gs);
    {   // online race entry
        bool on = g_menuSel == MI_ONLINE;
        const char* ol = TR("ONLINE RACE (WI-FI)");
        int ow = textWidth(ol);
        if (on) blendRect(W / 2 - ow / 2 - 8, py + 137, ow + 16, 13, rgb(60, 160, 255), 120);
        text(W / 2 - ow / 2, py + 140, ol, on ? rgb(255, 255, 255) : rgb(150, 190, 235));
    }
}

void draw_lights() {
    float t = g_stateT;
    int lit = t < 1.0f ? 0 : (t < 2.0f ? 1 : (t < 3.0f ? 2 : 3));
    bool green = t >= 4.0f;
    if (t > 5.0f) return;
    int cx = W / 2, y = 64;
    panel(cx - 54, y - 18, 108, 36);
    for (int i = 0; i < 3; ++i) {
        int x = cx - 32 + i * 32;
        uint16_t c = rgb(60, 10, 14);
        if (green) c = rgb(60, 255, 90);
        else if (i < lit) c = rgb(255, 40, 30);
        fillCircle(x, y, 12, rgb(10, 10, 14));
        fillCircle(x, y, 10, c);
        if (green || i < lit) {
            glowCircle(x, y, 22, green ? rgb(30, 120, 40) : rgb(120, 20, 10));
            fillCircle(x - 3, y - 3, 3, lerp(c, 0xFFFF, 160));
        }
    }
    if (t < 1.0f) textCentered(cx, y + 26, TR("GET READY"), rgb(255, 255, 255), rgb(0, 0, 0), fit_scale(TR("GET READY"), 2));
}

void draw_message() {
    if (g_msgT <= 0) return;
    int s = 3;
    while (s > 1 && textWidth(g_msg, s) > W - 8) --s;      // translated texts can be longer: shrink to fit
    int w = textWidth(g_msg, s);
    int y = 92 + (g_msgT > 1.4f ? (int)((g_msgT - 1.4f) * 80) : 0);
    if (((int)(g_msgT * 10) & 1) || g_msgT < 1.0f)
        textFancy(W / 2 - w / 2, y, g_msg, g_msgTop, g_msgBot, rgb(0, 0, 0), s);
}

void draw_pause() {
    darkRect(0, 0, W, H);
    panel(W / 2 - 80, 70, 160, 100);
    textCentered(W / 2, 80, TR("PAUSED"), rgb(255, 230, 90), rgb(0, 0, 0), fit_scale(TR("PAUSED"), 3));
    const char* items[3] = { TR("RESUME"), g_onlineRace ? TR("(LIVE RACE)") : TR("RESTART"), g_onlineRace ? TR("LEAVE THE RACE") : TR("QUIT TO MENU") };
    for (int i = 0; i < 3; ++i) {
        int y = 116 + i * 16;
        bool sel = g_pauseSel == i;
        if (sel) blendRect(W / 2 - 70, y - 3, 140, 14, rgb(255, 200, 40), 90);
        textCentered(W / 2, y, items[i], sel ? rgb(255, 255, 255) : rgb(170, 170, 200), rgb(0, 0, 0));
    }
}

void draw_results() {
    render_world(g_race, g_time);
    darkRect(0, 0, W, H);
    const char* title = TR("RACE RESULTS");
    { int sc = fit_scale(title, 3); textFancy(W / 2 - textWidth(title, sc) / 2, 8, title, rgb(255, 250, 150), rgb(255, 120, 30), rgb(40, 0, 0), sc); }
    panel(12, 40, W - 24, 152);
    text(22, 46, TR("POS"), rgb(150, 150, 190));
    text(62, 46, TR("DRIVER"), rgb(150, 150, 190));
    text(170, 46, TR("TIME"), rgb(150, 150, 190));
    text(244, 46, TR("BEST LAP"), rgb(150, 150, 190));
    int order[CAR_COUNT];
    for (int k = 0; k < CAR_COUNT; ++k) order[g_race.cars[k].place - 1] = k;
    float winner = g_race.cars[order[0]].finishTime;
    int shown = (int)(g_stateT * 10);
    for (int i = 0; i < CAR_COUNT && i < shown; ++i) {
        const Car& c = g_race.cars[order[i]];
        int y = 60 + i * 16;
        if (c.isPlayer) blendRect(14, y - 4, W - 28, 15, rgb(255, 200, 40), 90);
        char buf[24], t[16];
        snprintf(buf, sizeof buf, "%d", i + 1);
        uint16_t pc = i == 0 ? rgb(255, 215, 0) : i == 1 ? rgb(200, 210, 220) : i == 2 ? rgb(220, 140, 70) : rgb(255, 255, 255);
        textOutline(26, y, buf, pc, rgb(0, 0, 0));
        fillRect(44, y, 12, 8, c.livery.body);
        rect(44, y, 12, 8, c.livery.stripe);
        textOutline(62, y, c.name, c.isPlayer ? rgb(255, 240, 120) : rgb(255, 255, 255), rgb(0, 0, 0));
        if (c.retired) snprintf(t, sizeof t, "%s", TR("DNF"));
        else if (i == 0) format_time(t, c.finishTime);
        else { float gap = c.finishTime - winner; snprintf(t, sizeof t, "+%d.%02d", (int)gap, (int)(gap * 100) % 100); }
        textOutline(170, y, t, rgb(255, 255, 255), rgb(0, 0, 0));
        if (c.bestLap > 0) { format_time(t, c.bestLap); textOutline(244, y, t, rgb(200, 220, 255), rgb(0, 0, 0)); }
    }
    const Car& p = g_race.player();
    char buf[40];
    if (p.retired) snprintf(buf, sizeof buf, "%s", TR("ENGINE FAILURE"));
    else snprintf(buf, sizeof buf, "%s %d%s", TR("YOU FINISHED"), p.place, i18n::ordinal(p.place));
    textCentered(W / 2, 198, buf, p.place == 1 ? rgb(255, 230, 60) : rgb(255, 255, 255), rgb(0, 0, 0), 2);
    if ((g_newLapRecord || g_newRaceRecord) && ((int)(g_time * 3) & 1))
        textCentered(W / 2, 216, g_newRaceRecord ? TR("NEW RACE RECORD!") : TR("NEW LAP RECORD!"), rgb(120, 255, 160), rgb(0, 0, 0));
    if (g_stateT > 1.5f) { const char* f = g_onlineRace ? TR("A: BACK TO LOBBY   B: LEAVE") : TR("A: RACE AGAIN   B: MENU"); text(W / 2 - textWidth(f) / 2, 230, f, rgb(200, 200, 230)); }
}

void draw_online() {
    render_world(g_race, g_time);
    darkRect(0, 0, W, H);
    const char* title = TR("ONLINE RACE");
    { int sc = fit_scale(title, 3); textFancy(W / 2 - textWidth(title, sc) / 2, 6, title, rgb(170, 230, 255), rgb(60, 130, 255), rgb(0, 0, 30), sc); }
    panel(12, 34, W - 24, 196);
    const online::Info& in = online::info();
    int st = online::status();
    char buf[64];
    const uint16_t white = rgb(255, 255, 255), grey = rgb(150, 150, 190);
    if (g_loadRes != online::LOAD_OK) {
        textCentered(W / 2, 46, g_loadRes == online::LOAD_NO_FILE ? TR("SETTINGS FILE NOT FOUND") : TR("NO WI-FI NAME IN THE FILE"), rgb(255, 150, 110), rgb(0, 0, 0));
        text(24, 66, TR("Create this text file on the SD card:"), grey);
        text(24, 80, online::settingsPath(), rgb(255, 230, 120));
        text(24, 100, "ssid=MyWifiName", white);
        text(24, 112, "pass=MyWifiPassword", white);
        text(24, 124, "room=AKA1", white);
        text(24, 136, "name=PILOT", white);
        text(24, 156, TR("The public race server is already filled in."), grey);
        text(24, 168, TR("Friends use the same room name to race together."), grey);
        text(24, 180, TR("See online.example.txt in the sdcard pack."), grey);
        text(W / 2 - textWidth(TR("B: BACK")) / 2, 214, TR("B: BACK"), rgb(200, 200, 230));
        return;
    }
    // status line
    const char* msg = "";
    uint16_t mc = rgb(255, 230, 120);
    if (st == plat::NET_WIFI) msg = TR("JOINING WI-FI...");
    else if (st == plat::NET_CONNECTING) msg = TR("CONNECTING TO THE SERVER...");
    else if (st == plat::NET_ONLINE && !in.haveSnap) msg = TR("ENTERING THE ROOM...");
    else if (st == plat::NET_ONLINE) { snprintf(buf, sizeof buf, "%s %s - %s", TR("ROOM"), g_set.room, in.host ? TR("YOU ARE THE HOST") : TR("WAITING FOR THE HOST")); msg = buf; mc = rgb(140, 255, 170); }
    else if (st == plat::NET_FAILED) { snprintf(buf, sizeof buf, "%s: %s", TR("ERROR"), TR(plat::netError())); msg = buf; mc = rgb(255, 130, 110); }
    else msg = "...";
    textCentered(W / 2, 42, msg, mc, rgb(0, 0, 0));
    if (st == plat::NET_CONNECTING && g_stateT > 6) { const char* w = TR("The free server may need a minute to wake up."); text(W / 2 - textWidth(w) / 2, 54, w, grey); }
    if (in.haveSnap) {
        snprintf(buf, sizeof buf, "%s  -  %d %s", g_trackDefs[in.track % TRACK_COUNT].name, in.laps, in.laps > 1 ? TR("LAPS") : TR("LAP"));
        textCentered(W / 2, 56, buf, white, rgb(0, 0, 0));
        for (int i = 0; i < CAR_COUNT; ++i) {
            const Car& c = g_race.cars[i];
            int y = 72 + i * 15;
            bool me = in.haveWelcome && i == in.slot;
            bool human = (in.humanMask >> i) & 1;
            if (me) blendRect(16, y - 3, W - 32, 14, rgb(255, 200, 40), 90);
            fillRect(24, y, 12, 8, c.livery.body);
            rect(24, y, 12, 8, c.livery.stripe);
            snprintf(buf, sizeof buf, "%d", i + 1);
            text(42, y, buf, grey);
            textOutline(60, y, c.name, white, rgb(0, 0, 0));
            const char* tag = me ? (in.host ? TR("YOU (HOST)") : TR("YOU")) : (human ? TR("PLAYER") : TR("COMPUTER"));
            uint16_t tc = me ? rgb(255, 240, 120) : (human ? rgb(120, 255, 160) : rgb(130, 130, 160));
            text(150, y, tag, tc);
        }
    }
    const char* hint = TR("B: LEAVE");
    if (st == plat::NET_ONLINE && in.haveSnap) {
        if (in.phase != online::PH_LOBBY) hint = TR("RACE IN PROGRESS...");
        else if (in.host) { hint = TR("A: START RACE   B: LEAVE"); const char* h2 = TR("LEFT/RIGHT: TRACK   UP/DOWN: LAPS"); text(W / 2 - textWidth(h2) / 2, 204, h2, grey); }
        else hint = TR("WAITING FOR THE HOST...");
    }
    text(W / 2 - textWidth(hint) / 2, 216, hint, rgb(200, 200, 230));
    if (plat::freeInternalKb()) { char rb[16]; snprintf(rb, sizeof rb, "RAM %uK", plat::freeInternalKb()); text(W - 20 - textWidth(rb), 232, rb, rgb(90, 90, 130)); }
}

void audio_update() {
    const Race& r = g_race;
    bool active = !g_paused && (g_state == ST_COUNTDOWN || g_state == ST_RACING || g_state == ST_FINISHED);
    float vol = active ? 0.55f : 0.0f;
    if (g_state == ST_FINISHED) vol *= fmaxf(0.0f, 1.0f - g_stateT / 5.0f);
    // damaged engine: rough, weaker, cutting out; dead engine: silent
    float dmg = r.damage;
    if (r.player().retired) vol *= 0.0f;
    else if (r.sputter > 0) vol *= 0.15f;
    else if (dmg > 0.4f) vol *= 1.0f - 0.35f * ((int)(g_time * 23) % 3 == 0 ? 1.0f : 0.0f) * (dmg - 0.4f) / 0.6f;
    plat::setEngine(r.rpm * (1.0f - 0.25f * dmg), r.nitroOn ? 1.0f : 0.6f * (1.0f - 0.4f * dmg), vol);
    plat::setNoise(active ? r.offroad : 0, active ? r.skid : 0);
}

void handle_events() {
    const RaceEvents& e = g_race.ev;
    if (e.hit) {
        static const int sfx[] = { 0, plat::SFX_HIT_SOFT, plat::SFX_HIT_WOOD, plat::SFX_HIT_ROCK, plat::SFX_HIT_METAL,
                                   plat::SFX_HIT_ICE, plat::SFX_HIT_HEAVY, plat::SFX_BUMP, plat::SFX_SCRAPE };
        plat::playSfx(sfx[e.hit]);
        fx_emit(g_race);
    } else if (e.crash) {
        plat::playSfx(plat::SFX_CRASH);
        fx_burst(W / 2 + (g_race.player().x > 0 ? 50 : -50), H - 40, 22, rgb(255, 220, 90), 140, 0.6f);
    } else if (e.bump) {
        plat::playSfx(plat::SFX_BUMP);
        fx_burst(W / 2, H - 70, 10, rgb(255, 230, 120), 110, 0.4f);
    }
    {   // engine damage warnings
        static float lastD = 0;
        float d = g_race.damage;
        if (d < lastD) lastD = d;
        if (lastD < 0.5f && d >= 0.5f) message("ENGINE DAMAGED", rgb(255, 210, 90), rgb(190, 90, 10), 2.0f);
        else if (lastD < 0.8f && d >= 0.8f) message("ENGINE FAILING!", rgb(255, 120, 90), rgb(190, 20, 10), 2.2f);
        lastD = d;
    }
    if (e.pitStart) message("PIT STOP", rgb(255, 230, 120), rgb(200, 120, 10), 1.5f);
    if (e.pitDone) { plat::playSfx(plat::SFX_LAP); message(g_race.damage <= 0.01f ? "ENGINE REPAIRED" : "TYRES & TRACKING OK", rgb(160, 255, 200), rgb(20, 160, 90), 1.8f); }
    {   // pit lane: speed limiter, bay names, lollipop
        static bool wasApron = false; static int lastBay = 0, lastLol = 0;
        if (g_race.onApron && !wasApron) message("PIT LANE - 85 KM/H", rgb(130, 210, 255), rgb(20, 90, 170), 1.4f);
        if (g_race.activeBay != lastBay) {
            if (g_race.activeBay == 1) message("N2O EXPRESS", rgb(120, 235, 255), rgb(20, 120, 160), 1.1f);
            else if (g_race.activeBay == 2) message("TYRES - STOP", rgb(255, 225, 110), rgb(170, 120, 10), 1.3f);
            else if (g_race.activeBay == 3) message("ENGINE GARAGE - STOP", rgb(255, 130, 110), rgb(170, 30, 20), 1.3f);
        }
        if (g_race.lollipop == 3 && lastLol != 3) message("GO! GO! GO!", rgb(150, 255, 170), rgb(20, 150, 60), 1.5f);
        wasApron = g_race.onApron; lastBay = g_race.activeBay; lastLol = g_race.lollipop;
    }
    if (e.lap && g_race.damage > 0.25f && !g_race.player().retired) message("PIT LANE ON THE RIGHT", rgb(255, 230, 120), rgb(200, 120, 10), 2.0f);
    if (e.retired) {
        plat::playSfx(plat::SFX_HIT_HEAVY);
        message("ENGINE DEAD", rgb(255, 90, 70), rgb(120, 10, 10), 4.0f);
        g_state = ST_FINISHED; g_stateT = 0;
        g_race.autopilot = true;
    }
    if (e.finished) {
        plat::playSfx(plat::SFX_FINISH);
        const Car& p = g_race.player();
        static const char* const pos[] = { "", "1ST PLACE!", "2ND PLACE", "3RD PLACE", "4TH PLACE", "5TH PLACE", "6TH PLACE", "7TH PLACE", "8TH PLACE" };
        message(p.place == 1 ? "WINNER!" : "FINISH!", rgb(255, 255, 160), rgb(255, 90, 30), 5.0f);
        (void)pos;
        g_state = ST_FINISHED; g_stateT = 0;
        g_race.autopilot = true;
    } else if (e.lap) {
        plat::playSfx(plat::SFX_LAP);
        if (e.finalLap) message("FINAL LAP", rgb(255, 120, 120), rgb(200, 0, 40), 2.0f);
        else if (e.bestLap) message("BEST LAP!", rgb(160, 255, 200), rgb(20, 160, 90), 1.8f);
        else {
            char b[16]; snprintf(b, sizeof b, "LAP %d", g_race.playerLap());
            message(b, rgb(255, 255, 255), rgb(120, 190, 255), 1.6f);
        }
    }
}

} // namespace

// =================================================================== API
void game_set_framebuffer(uint16_t* p) { gfx::fb = p; }

void game_request_title() { g_titleRequest = true; }

void game_init() {
    sprites_init();
    music_init();
    load_records();
    apply_music_volume();
    go_title();
}

bool game_paused() { return g_paused || g_state == ST_TITLE || g_state == ST_RESULTS || g_state == ST_ONLINE; }

Race* game_debug_race() { return &g_race; }
void game_debug_select(int track, int difficulty, int lapsIndex) {
    g_track = track; g_diff = difficulty; g_lapsSel = lapsIndex;
    start_demo();
}

void game_frame(const plat::Input& in, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    g_time += dt;
    g_stateT += dt;
    if (g_msgT > 0) g_msgT -= dt;
    plat::Input none;
    i18n::setCode(plat::languageCode());
    if (g_titleRequest) {
        g_titleRequest = false;
        if (g_state != ST_TITLE) go_title();
    }

    switch (g_state) {
    case ST_TITLE: {
        if (in.pressed & plat::BTN_UP) { g_menuSel = (g_menuSel + MENU_ITEMS - 1) % MENU_ITEMS; plat::playSfx(plat::SFX_MENU_MOVE); }
        if (in.pressed & plat::BTN_DOWN) { g_menuSel = (g_menuSel + 1) % MENU_ITEMS; plat::playSfx(plat::SFX_MENU_MOVE); }
        int d = (in.pressed & plat::BTN_RIGHT) ? 1 : ((in.pressed & plat::BTN_LEFT) ? -1 : 0);
        if (d) {
            plat::playSfx(plat::SFX_MENU_MOVE);
            if (g_menuSel == 0) { g_track = (g_track + d + TRACK_COUNT) % TRACK_COUNT; start_demo(); }
            if (g_menuSel == 1) g_diff = (g_diff + d + DIFF_COUNT) % DIFF_COUNT;
            if (g_menuSel == 2) g_lapsSel = (g_lapsSel + d + 4) % 4;
            if (g_menuSel == 3) { g_rec.musicOff ^= 1; apply_music_volume(); plat::saveBlob(&g_rec, sizeof g_rec); }
            if (g_menuSel == MI_LANG) plat::setLanguageCode(i18n::code((i18n::language() + d + i18n::COUNT) % i18n::COUNT));
        }
        if (in.pressed & (plat::BTN_A | plat::BTN_RUN)) {
            if (g_menuSel == MI_ONLINE && (in.pressed & plat::BTN_A)) { go_online(); break; }
            if (g_menuSel == MI_NAME && (in.pressed & plat::BTN_A)) { open_name_editor(); break; }
            if (g_menuSel == MI_START || (in.pressed & plat::BTN_RUN)) { start_race(); break; }
            if (g_menuSel == 3) { g_rec.musicOff ^= 1; apply_music_volume(); plat::saveBlob(&g_rec, sizeof g_rec); }
            else if (g_menuSel == MI_LANG) plat::setLanguageCode(i18n::code((i18n::language() + 1) % i18n::COUNT));
            else g_menuSel = MI_START;
            plat::playSfx(plat::SFX_MENU_MOVE);
        }
        for (float t = dt; t > 0; t -= 1.0f / 60) g_race.update(fminf(t, 1.0f / 60), none);
        draw_title();
        break;
    }
    case ST_COUNTDOWN: {
        if (g_countBeep < 3 && g_stateT >= 1.0f + g_countBeep) { g_countBeep++; plat::playSfx(plat::SFX_BEEP_LOW); }
        if (g_stateT >= 4.0f) {
            plat::playSfx(plat::SFX_BEEP_HIGH);
            g_race.start();
            g_state = ST_RACING;
            message("GO!", rgb(160, 255, 160), rgb(20, 200, 60), 1.2f);
        }
        if (g_onlineRace) online_tick(dt);
        g_race.update(dt, in);
        render_world(g_race, g_time);
        render_hud(g_race, g_time);
        draw_lights();
        break;
    }
    case ST_RACING:
    case ST_FINISHED: {
        if (!g_paused && (in.pressed & plat::BTN_RUN)) { g_paused = true; g_pauseSel = 0; plat::playSfx(plat::SFX_MENU_MOVE); }
        else if (g_paused) {
            if (in.pressed & plat::BTN_UP) { g_pauseSel = (g_pauseSel + 2) % 3; plat::playSfx(plat::SFX_MENU_MOVE); }
            if (in.pressed & plat::BTN_DOWN) { g_pauseSel = (g_pauseSel + 1) % 3; plat::playSfx(plat::SFX_MENU_MOVE); }
            if (in.pressed & plat::BTN_RUN) g_paused = false;
            if (in.pressed & plat::BTN_A) {
                plat::playSfx(plat::SFX_MENU_OK);
                if (g_pauseSel == 0) g_paused = false;
                else if (g_pauseSel == 1) { g_paused = false; if (!g_onlineRace) { start_race(); break; } }
                else { go_title(); break; }
            }
        }
        if (!g_paused || g_onlineRace) {      // a live online race never freezes
            if (g_onlineRace) online_tick(dt);
            // fixed physics step
            for (float t = dt; t > 1e-4f; t -= 1.0f / 60) {
                g_race.update(fminf(t, 1.0f / 60), g_paused ? none : in);
                handle_events();
            }
            fx_update(g_race, dt);
        }
        render_world(g_race, g_time);
        fx_draw();
        if (g_state == ST_RACING || g_stateT < 4.5f) render_hud(g_race, g_time);
        draw_message();
        if (g_state == ST_FINISHED) {
            const Car& p = g_race.player();
            char b[24]; snprintf(b, sizeof b, "POSITION %d / %d", p.place, CAR_COUNT);
            if (g_stateT > 0.8f) textCentered(W / 2, 130, b, rgb(255, 255, 255), rgb(0, 0, 0), 2);
            if (g_stateT > 5.0f || (g_stateT > 1.5f && (in.pressed & plat::BTN_A))) finish_race();
        }
        if (g_paused) draw_pause();
        break;
    }
    case ST_NAME: {
        update_name_editor(in, dt);
        for (float t = dt; t > 0; t -= 1.0f / 60) g_race.update(fminf(t, 1.0f / 60), none);
        draw_name_editor();
        break;
    }
    case ST_ONLINE: {
        const online::Info& oi = online::info();
        if (g_online) { online::poll(); online::applyNames(g_race); }
        if (g_online && online::isOnline() && oi.haveWelcome && oi.host && !g_cfgSent) {
            g_cfgSent = true;
            online::sendConfigure(g_track, kLaps[g_lapsSel], g_diff);
        }
        bool ready = g_online && online::isOnline() && oi.haveSnap;
        if (ready && oi.phase == online::PH_COUNTDOWN && g_lastPhase != online::PH_COUNTDOWN) { start_online_race(); break; }
        g_lastPhase = oi.phase;
        if (ready && !oi.host) {             // follow what the host chose
            if (g_track != oi.track % TRACK_COUNT) { g_track = oi.track % TRACK_COUNT; start_demo(); }
        }
        if (in.pressed & (plat::BTN_B | plat::BTN_RUN)) { plat::playSfx(plat::SFX_MENU_OK); go_title(); break; }
        if (ready && oi.host && oi.phase == online::PH_LOBBY) {
            int d = (in.pressed & plat::BTN_RIGHT) ? 1 : ((in.pressed & plat::BTN_LEFT) ? -1 : 0);
            int u = (in.pressed & plat::BTN_DOWN) ? 1 : ((in.pressed & plat::BTN_UP) ? -1 : 0);
            if (d) { g_track = (g_track + d + TRACK_COUNT) % TRACK_COUNT; start_demo(); plat::playSfx(plat::SFX_MENU_MOVE); online::sendConfigure(g_track, kLaps[g_lapsSel], g_diff); }
            if (u) { g_lapsSel = (g_lapsSel + u + 4) % 4; plat::playSfx(plat::SFX_MENU_MOVE); online::sendConfigure(g_track, kLaps[g_lapsSel], g_diff); }
            if (in.pressed & plat::BTN_A) { plat::playSfx(plat::SFX_MENU_OK); online::sendStart(); }
        }
        for (float t = dt; t > 0; t -= 1.0f / 60) g_race.update(fminf(t, 1.0f / 60), none);
        draw_online();
        break;
    }
    case ST_RESULTS: {
        for (float t = dt; t > 1e-4f; t -= 1.0f / 60) g_race.update(fminf(t, 1.0f / 60), none);
        if (g_onlineRace) { online::poll(); online::applyNames(g_race); if (!online::isOnline()) { go_title(); break; } }
        if (g_stateT > 1.0f) {
            if (g_onlineRace) {
                if (in.pressed & plat::BTN_A) { plat::playSfx(plat::SFX_MENU_OK); if (online::info().host) online::sendLobby(); enter_online_lobby(); break; }
                if (in.pressed & (plat::BTN_B | plat::BTN_RUN)) { plat::playSfx(plat::SFX_MENU_OK); go_title(); break; }
            }
            else if (in.pressed & plat::BTN_A) start_race();
            else if (in.pressed & (plat::BTN_B | plat::BTN_RUN)) { plat::playSfx(plat::SFX_MENU_OK); go_title(); }
        }
        if (g_state == ST_RESULTS) draw_results();
        else render_world(g_race, g_time);
        break;
    }
    }
    audio_update();
}

bool game_is_title() { return g_state == ST_TITLE; }
