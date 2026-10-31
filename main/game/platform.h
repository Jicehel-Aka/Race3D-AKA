// platform.h - thin hardware abstraction so the game code builds both for
// the Gamebuino AKA (ESP-IDF, main.cpp) and for the PC preview (host/).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace plat {

enum Button : uint32_t {
    BTN_UP = 1u << 0, BTN_DOWN = 1u << 1, BTN_LEFT = 1u << 2, BTN_RIGHT = 1u << 3,
    BTN_A = 1u << 4, BTN_B = 1u << 5, BTN_C = 1u << 6, BTN_D = 1u << 7,
    BTN_L1 = 1u << 8, BTN_R1 = 1u << 9, BTN_RUN = 1u << 10,
};

struct Input {
    uint32_t held = 0;      // buttons currently down
    uint32_t pressed = 0;   // buttons that went down this frame
    float joyX = 0, joyY = 0; // analog stick, -1..1 (dead zone applied)
};

enum Sfx : int {
    SFX_BEEP_LOW = 0, SFX_BEEP_HIGH, SFX_MENU_MOVE, SFX_MENU_OK, SFX_CRASH,
    SFX_BUMP, SFX_NITRO, SFX_LAP, SFX_FINISH,
    // impacts by material
    SFX_HIT_SOFT, SFX_HIT_WOOD, SFX_HIT_ROCK, SFX_HIT_METAL, SFX_HIT_ICE, SFX_HIT_HEAVY, SFX_SCRAPE,
    SFX_COUNT
};

uint32_t millis();
void*    allocLarge(size_t bytes);              // PSRAM when available
void     freeLarge(void* p);

// Audio. Engine values are read continuously by the audio thread.
void setEngine(float rpm01, float throttle, float volume);
void setNoise(float rumble, float skid);        // 0..1 offroad rumble / tyre squeal
void playSfx(int id);
void playMusic(int song, bool restart = false);   // song index (music.h), -1 = stop
void setMusicVolume(float v);                     // 0..1

// Tiny persistent store (records).
bool saveBlob(const void* data, size_t size);
bool loadBlob(void* data, size_t size);

} // namespace plat
