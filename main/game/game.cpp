// game.cpp - title screen, countdown, race flow, results and records.
#include "game.h"
#include "gfx.h"
#include "race.h"
#include "render.h"
#include "sprites.h"
#include "music.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace gfx;

namespace {

enum State { ST_TITLE, ST_COUNTDOWN, ST_RACING, ST_FINISHED, ST_RESULTS };

struct Records {
    uint32_t magic;
    float bestLap[TRACK_COUNT];
    float bestRace[TRACK_COUNT][DIFF_COUNT];
    uint8_t bestPlace[TRACK_COUNT][DIFF_COUNT];
    uint8_t musicOff;
};
constexpr uint32_t REC_MAGIC = 0x52443303;

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

int g_menuSel = 4;
constexpr int MENU_ITEMS = 5;   // track, difficulty, laps, music, start
volatile bool g_titleRequest = false;
int g_track = 0, g_diff = DIFF_NORMAL, g_lapsSel = 2;
const int kLaps[] = { 1, 2, 3, 5 };

char g_msg[24] = "";
float g_msgT = 0;
uint16_t g_msgTop = 0, g_msgBot = 0;
bool g_newLapRecord = false, g_newRaceRecord = false;
int g_countBeep = 0;

const char* const kDiffDesc[DIFF_COUNT] = {
    "Relaxed rivals who make mistakes",
    "A balanced field, a fair fight",
    "Fast and sharp. Use your nitro!",
    "Pro drivers. Perfect lines only.",
};

void message(const char* s, uint16_t top, uint16_t bot, float dur = 1.6f) {
    strncpy(g_msg, s, sizeof g_msg - 1);
    g_msgTop = top; g_msgBot = bot; g_msgT = dur;
}

void load_records() {
    if (plat::loadBlob(&g_rec, sizeof g_rec) && g_rec.magic == REC_MAGIC) return;
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
    plat::playMusic(g_trackDefs[g_track].music);
    g_race.setup(g_track, DIFF_HARD, 99, true);
    g_race.cameraZ();
    // spread the field so the attract mode shows some traffic
    for (int i = 0; i < CAR_COUNT; ++i) g_race.cars[i].dist += SEG_LEN * 40;
    fx_clear();
}

void go_title() {
    g_state = ST_TITLE; g_stateT = 0; g_paused = false;
    start_demo();
}

void start_race() {
    g_race.setup(g_track, g_diff, kLaps[g_lapsSel], false);
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

void draw_title() {
    render_world(g_race, g_time);
    // vignette at the top for the logo
    for (int y = 0; y < 70; ++y) blendRect(0, y, W, 1, rgb(0, 0, 0), (70 - y) * 2);
    draw_logo(10);

    const int px = 24, py = 112, pw = W - 48, ph = 122;
    panel(px, py, pw, ph);
    char buf[48];
    const char* labels[4] = { "TRACK", "DIFFICULTY", "LAPS", "MUSIC" };
    for (int i = 0; i < 4; ++i) {
        int y = py + 7 + i * 15;
        bool sel = g_menuSel == i;
        if (sel) blendRect(px + 4, y - 3, pw - 8, 14, rgb(255, 200, 40), 80);
        text(px + 12, y, labels[i], sel ? rgb(255, 230, 90) : rgb(170, 170, 200));
        const char* v = i == 0 ? g_trackDefs[g_track].name : (i == 1 ? g_diffNames[g_diff] : nullptr);
        if (i == 2) { snprintf(buf, sizeof buf, "%d", kLaps[g_lapsSel]); v = buf; }
        if (i == 3) v = g_rec.musicOff ? "OFF" : g_songs[g_trackDefs[g_track].music].title;
        int vx = px + 110;
        if (sel) {
            bool blink = ((int)(g_time * 4) & 1);
            text(vx - 14 - (blink ? 1 : 0), y, "<", rgb(255, 255, 255));
            text(vx + 132 + (blink ? 1 : 0), y, ">", rgb(255, 255, 255));
        }
        uint16_t vc = rgb(255, 255, 255);
        if (i == 3 && g_rec.musicOff) vc = rgb(150, 150, 170);
        if (i == 1) { const uint16_t dc[] = { rgb(120, 255, 140), rgb(140, 210, 255), rgb(255, 190, 70), rgb(255, 80, 80) }; vc = dc[g_diff]; }
        textOutline(vx + 66 - textWidth(v) / 2, y, v, vc, rgb(0, 0, 0));
    }
    text(px + 12, py + 68, kDiffDesc[g_diff], rgb(150, 150, 180));

    // records for the selection
    char t[16];
    if (g_rec.bestLap[g_track] > 0) { format_time(t, g_rec.bestLap[g_track]); snprintf(buf, sizeof buf, "LAP RECORD %s", t); }
    else snprintf(buf, sizeof buf, "LAP RECORD --'--\"--");
    text(px + 12, py + 80, buf, rgb(255, 220, 110));
    uint8_t bp = g_rec.bestPlace[g_track][g_diff];
    if (bp) { snprintf(buf, sizeof buf, "BEST: P%d", bp); text(px + pw - 12 - textWidth(buf), py + 80, buf, rgb(255, 220, 110)); }

    bool sel = g_menuSel == 4;
    const char* go = "START RACE";
    int k = sel ? (int)(sinf(g_time * 6) * 60 + 196) : 140;
    int sw = textWidth(go, 2);
    if (sel) blendRect(W / 2 - sw / 2 - 10, py + 96, sw + 20, 20, rgb(255, 60, 40), 120);
    textFancy(W / 2 - sw / 2, py + 98, go, scale(rgb(255, 255, 255), k), scale(rgb(255, 210, 60), k), rgb(40, 0, 0), 2);
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
    if (t < 1.0f) textCentered(cx, y + 26, "GET READY", rgb(255, 255, 255), rgb(0, 0, 0), 2);
}

void draw_message() {
    if (g_msgT <= 0) return;
    int s = 3;
    int w = textWidth(g_msg, s);
    int y = 92 + (g_msgT > 1.4f ? (int)((g_msgT - 1.4f) * 80) : 0);
    if (((int)(g_msgT * 10) & 1) || g_msgT < 1.0f)
        textFancy(W / 2 - w / 2, y, g_msg, g_msgTop, g_msgBot, rgb(0, 0, 0), s);
}

void draw_pause() {
    darkRect(0, 0, W, H);
    panel(W / 2 - 80, 70, 160, 100);
    textCentered(W / 2, 80, "PAUSED", rgb(255, 230, 90), rgb(0, 0, 0), 3);
    const char* items[3] = { "RESUME", "RESTART", "QUIT TO MENU" };
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
    const char* title = "RACE RESULTS";
    textFancy(W / 2 - textWidth(title, 3) / 2, 8, title, rgb(255, 250, 150), rgb(255, 120, 30), rgb(40, 0, 0), 3);
    panel(12, 40, W - 24, 152);
    text(22, 46, "POS", rgb(150, 150, 190));
    text(62, 46, "DRIVER", rgb(150, 150, 190));
    text(170, 46, "TIME", rgb(150, 150, 190));
    text(244, 46, "BEST LAP", rgb(150, 150, 190));
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
        if (c.retired) snprintf(t, sizeof t, "DNF");
        else if (i == 0) format_time(t, c.finishTime);
        else { float gap = c.finishTime - winner; snprintf(t, sizeof t, "+%d.%02d", (int)gap, (int)(gap * 100) % 100); }
        textOutline(170, y, t, rgb(255, 255, 255), rgb(0, 0, 0));
        if (c.bestLap > 0) { format_time(t, c.bestLap); textOutline(244, y, t, rgb(200, 220, 255), rgb(0, 0, 0)); }
    }
    const Car& p = g_race.player();
    char buf[40];
    static const char* const ord[] = { "", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH" };
    if (p.retired) snprintf(buf, sizeof buf, "ENGINE FAILURE");
    else snprintf(buf, sizeof buf, "YOU FINISHED %s", ord[p.place]);
    textCentered(W / 2, 198, buf, p.place == 1 ? rgb(255, 230, 60) : rgb(255, 255, 255), rgb(0, 0, 0), 2);
    if ((g_newLapRecord || g_newRaceRecord) && ((int)(g_time * 3) & 1))
        textCentered(W / 2, 216, g_newRaceRecord ? "NEW RACE RECORD!" : "NEW LAP RECORD!", rgb(120, 255, 160), rgb(0, 0, 0));
    if (g_stateT > 1.5f) text(W / 2 - textWidth("A: RACE AGAIN   B: MENU") / 2, 230, "A: RACE AGAIN   B: MENU", rgb(200, 200, 230));
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
    if (e.pitDone) { plat::playSfx(plat::SFX_LAP); message("ENGINE REPAIRED", rgb(160, 255, 200), rgb(20, 160, 90), 1.8f); }
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

bool game_paused() { return g_paused || g_state == ST_TITLE || g_state == ST_RESULTS; }

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
        }
        if (in.pressed & (plat::BTN_A | plat::BTN_RUN)) {
            if (g_menuSel == 4 || (in.pressed & plat::BTN_RUN)) { start_race(); break; }
            if (g_menuSel == 3) { g_rec.musicOff ^= 1; apply_music_volume(); plat::saveBlob(&g_rec, sizeof g_rec); }
            else g_menuSel = 4;
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
                else if (g_pauseSel == 1) { g_paused = false; start_race(); break; }
                else { go_title(); break; }
            }
        }
        if (!g_paused) {
            // fixed physics step
            for (float t = dt; t > 1e-4f; t -= 1.0f / 60) {
                g_race.update(fminf(t, 1.0f / 60), in);
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
    case ST_RESULTS: {
        for (float t = dt; t > 1e-4f; t -= 1.0f / 60) g_race.update(fminf(t, 1.0f / 60), none);
        if (g_stateT > 1.0f) {
            if (in.pressed & plat::BTN_A) start_race();
            else if (in.pressed & (plat::BTN_B | plat::BTN_RUN)) { plat::playSfx(plat::SFX_MENU_OK); go_title(); }
        }
        if (g_state == ST_RESULTS) draw_results();
        else render_world(g_race, g_time);
        break;
    }
    }
    audio_update();
}
