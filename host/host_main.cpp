// host_main.cpp - PC preview of the game (SDL2).
//   race3d.exe                 play in a window (arrows, X=gas, Z=brake, C=nitro, Enter=pause/start)
//   race3d.exe --shots DIR T   headless: drive a scripted race on track T and dump screenshots
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game/game.h"
#include "game/gfx.h"
#include "game/race.h"
#include "game/synth.h"

static uint16_t s_fb[gfx::W * gfx::H];
static Synth s_synth;
static uint32_t s_fakeMs = 0;
static bool s_headless = false;

namespace plat {
uint32_t millis() { return s_headless ? s_fakeMs : SDL_GetTicks(); }
void* allocLarge(size_t n) { return malloc(n); }
void freeLarge(void* p) { free(p); }
void setEngine(float rpm, float thr, float vol) { s_synth.setEngine(rpm, thr, vol); }
void setNoise(float rumble, float skid) { s_synth.setNoise(rumble, skid); }
void playSfx(int id) { s_synth.play(id); }
void playMusic(int song, bool restart) { s_synth.setSong(song, restart); }
void setMusicVolume(float v) { s_synth.setMusicVolume(v); }
bool saveBlob(const void* d, size_t n) {
    if (s_headless) return true;
    FILE* f = fopen("race3d_records.bin", "wb"); if (!f) return false;
    fwrite(d, 1, n, f); fclose(f); return true;
}
bool loadBlob(void* d, size_t n) {
    if (s_headless) return false;
    FILE* f = fopen("race3d_records.bin", "rb"); if (!f) return false;
    size_t r = fread(d, 1, n, f); fclose(f); return r == n;
}
static char s_lang[4] = "en";
const char* languageCode() { return s_lang; }
void setLanguageCode(const char* code) {
    if (!code || !code[0]) return;
    snprintf(s_lang, sizeof s_lang, "%s", code);
    if (s_headless) return;
    FILE* f = fopen("race3d_lang.txt", "wb"); if (f) { fputs(s_lang, f); fclose(f); }
}
bool readTextFile(const char* path, char* buf, size_t cap) {
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;      // PC: online.txt next to the exe
    FILE* f = fopen(base, "rb"); if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f); fclose(f); buf[n] = 0; return true;
}
} // namespace plat

static void save_ppm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", gfx::W, gfx::H);
    for (int i = 0; i < gfx::W * gfx::H; ++i) {
        int r, g, b; gfx::unpack(s_fb[i], r, g, b);
        unsigned char px[3] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

// screenshot: 24-bit BMP in ./screenshots (opens everywhere on Windows)
static void save_bmp(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const int w = gfx::W, h = gfx::H, row = w * 3, size = 54 + row * h;     // 320*3 is a multiple of 4: no padding
    unsigned char hd[54] = { 'B', 'M' };
    auto put = [&](int off, uint32_t v, int n) { for (int i = 0; i < n; ++i) hd[off + i] = (unsigned char)(v >> (8 * i)); };
    put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4); put(26, 1, 2); put(28, 24, 2); put(34, row * h, 4);
    fwrite(hd, 1, 54, f);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            int r, g, b; gfx::unpack(s_fb[y * w + x], r, g, b);
            unsigned char px[3] = { (unsigned char)b, (unsigned char)g, (unsigned char)r };
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}

static void audio_cb(void*, Uint8* stream, int len) {
    s_synth.render((int16_t*)stream, len / 2, 44100);
}

// simple autopilot used by the screenshot script
static float auto_steer(Race& r) {
    const Car& p = r.player();
    const Seg& s = r.track.at(p.dist + SEG_LEN * 3);
    float pct = p.speed / MAX_SPEED;
    float want = s.curve * pct * 0.3f * 0.5f;   // counter the centrifugal drift
    float st = want * 4.0f - p.x * 1.5f;
    // dodge the car right ahead
    for (auto& c : r.cars) {
        if (&c == &p) continue;
        float dz = r.carZ(c) - r.carZ(p);
        if (dz > 0 && dz < SEG_LEN * 10 && fabsf(c.x - p.x) < 0.45f) st += (p.x > c.x ? 1.5f : -1.5f);
    }
    return st > 1 ? 1 : (st < -1 ? -1 : st);
}

static int run_shots(const char* dir, int track, bool full) {
    s_headless = true;
    gfx::fb = s_fb;
    game_init();
    game_debug_select(track, full ? 3 : 1, full ? 0 : 2);
    const float dt = 1.0f / 30;
    struct Shot { int frame; const char* name; };
    static const Shot normal[] = {
        { 60, "title" }, { 160, "countdown" }, { 330, "race_start" }, { 600, "race_a" },
        { 900, "race_b" }, { 1300, "race_c" }, { 1700, "race_d" },
    };
    static const Shot finish[] = { { 1500, "lap_end" }, { 1900, "finished" }, { 2050, "finished2" }, { 2350, "results" } };
    const Shot* shots = full ? finish : normal;
    int nshots = full ? 4 : 7, last = full ? 2350 : 1700;
    int si = 0;
    plat::Input prev;
    for (int f = 0; f <= last; ++f) {
        plat::Input in;
        if (f == 100) in.held |= plat::BTN_A;
        if (f > 260) {
            Race& r = *game_debug_race();
            in.held |= plat::BTN_A;
            in.joyX = auto_steer(r);
            if (f > 1200 && f < 1320) in.held |= plat::BTN_C;   // nitro
        }
        in.pressed = in.held & ~prev.held;
        prev = in;
        s_fakeMs += 33;
        gfx::fb = s_fb;
        game_frame(in, dt);
        if (si < nshots && shots[si].frame == f) {
            char path[256];
            snprintf(path, sizeof path, "%s/t%d_%s.ppm", dir, track, shots[si].name);
            save_ppm(path);
            ++si;
        }
    }
    Race& r = *game_debug_race();
    printf("track %d: %d segments, %zu props, player place %d, lap %d, dist %.0f, speed %.0f km/h\n",
           track, r.track.count(), r.track.props.size(), r.player().place, r.playerLap(), r.player().dist, r.speedKmh());
    for (auto& c : r.cars) printf("  %-6s dist %8.0f x %+.2f speed %5.0f place %d\n", c.name, c.dist, c.x, c.speed / MAX_SPEED * 240, c.place);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "--shots"))
        return run_shots(argv[2], argc >= 4 ? atoi(argv[3]) : 0, argc >= 5 && !strcmp(argv[4], "full"));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("AKA Turbo 3D Grand Prix - PC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       gfx::W * 3, gfx::H * 3, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, gfx::W, gfx::H);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING, gfx::W, gfx::H);

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);

    gfx::fb = s_fb;
    { FILE* lf = fopen("race3d_lang.txt", "rb"); if (lf) { char c[8] = ""; if (fgets(c, sizeof c, lf)) { c[2] = 0; plat::setLanguageCode(c); } fclose(lf); } }
    game_init();
    uint32_t last = SDL_GetTicks();
    uint32_t prevHeld = 0;
    bool running = true, fullscreen = false;
    SDL_GameController* pad = nullptr;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Scancode sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE && game_is_title()) running = false;            // Esc on the title screen = quit
                if (sc == SDL_SCANCODE_F1) game_request_title();                              // back to the menu
                if (sc == SDL_SCANCODE_F2) {                                                  // next language
                    static const char* const codes[] = { "en", "fr", "de", "es", "it" };
                    int cur = 0;
                    for (int i = 0; i < 5; ++i) if (!strcmp(plat::languageCode(), codes[i])) cur = i;
                    plat::setLanguageCode(codes[(cur + 1) % 5]);
                }
                if (sc == SDL_SCANCODE_F12) {                                                 // screenshot
#ifdef _WIN32
                    _mkdir("screenshots");
#else
                    mkdir("screenshots", 0777);
#endif
                    char path[96]; snprintf(path, sizeof path, "screenshots/shot_%u.bmp", (unsigned)SDL_GetTicks());
                    save_bmp(path);
                }
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
        }
        if (!pad)
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
        auto P = [&](SDL_GameControllerButton b) { return pad && SDL_GameControllerGetButton(pad, b); };
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        plat::Input in;
        if (k[SDL_SCANCODE_UP] || P(SDL_CONTROLLER_BUTTON_DPAD_UP)) in.held |= plat::BTN_UP;
        if (k[SDL_SCANCODE_DOWN] || P(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) in.held |= plat::BTN_DOWN;
        if (k[SDL_SCANCODE_LEFT] || P(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) in.held |= plat::BTN_LEFT;
        if (k[SDL_SCANCODE_RIGHT] || P(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) in.held |= plat::BTN_RIGHT;
        if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE] || P(SDL_CONTROLLER_BUTTON_A)) in.held |= plat::BTN_A;
        if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_W] || P(SDL_CONTROLLER_BUTTON_B)) in.held |= plat::BTN_B;
        if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_LSHIFT] || P(SDL_CONTROLLER_BUTTON_X) ||
            P(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) in.held |= plat::BTN_C;
        if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_ESCAPE] || P(SDL_CONTROLLER_BUTTON_START)) in.held |= plat::BTN_RUN;
        static uint32_t prevStick = 0;
        uint32_t stick = 0;
        if (pad) {   // stick analogique : direction progressive (joyX) + navigation des menus
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            in.joyX = fabsf(ax) > 0.15f ? (ax - (ax > 0 ? 0.15f : -0.15f)) / 0.85f : 0.0f;
            if (ax < -0.6f) stick |= plat::BTN_LEFT;
            if (ax > 0.6f) stick |= plat::BTN_RIGHT;
            if (ay < -0.6f) stick |= plat::BTN_UP;
            if (ay > 0.6f) stick |= plat::BTN_DOWN;
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) in.held |= plat::BTN_A;
            if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) in.held |= plat::BTN_B;
        }
        in.pressed = (in.held & ~prevHeld) | (stick & ~prevStick);   // le stick ne fait que des "appuis" (menus)
        prevHeld = in.held;
        prevStick = stick;

        uint32_t now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        gfx::fb = s_fb;
        game_frame(in, dt);

        SDL_UpdateTexture(tex, nullptr, s_fb, gfx::W * 2);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
