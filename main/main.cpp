// main.cpp - Gamebuino AKA entry point for AKA Turbo 3D Grand Prix.
// Hardware glue only: the game itself lives in game/ and is platform free.
#include "gb_core.h"
#include "gb_graphics.h"
#include "gb_ll_lcd.h"
#include "gb_ll_audio.h"
#include "gb_audio_player.h"
#include "core/input.h"
#include "aka_runtime/aka_runtime.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>

#include "game/game.h"
#include "game/synth.h"

gb_core     g_core;   // used by aka_runtime and core/input
gb_graphics gfx;      // used by the aka_runtime system menu

static const char* TAG = "race3d";
static const char* kRecordsPath = "/sdcard/race3d/records.bin";

// ------------------------------------------------------------------ audio
static Synth s_synth;

class SynthTrack : public gb_audio_track_base {
public:
    int play_callback(int16_t* buf, uint16_t n) override {
        s_synth.render(buf, n, GB_AUDIO_SAMPLE_RATE);
        return 0;
    }
    void stop_playing() override {}
    bool is_playing() override { return true; }
};

static gb_audio_player s_player;
static SynthTrack      s_track;

static void audio_task(void*) {
    while (true) {
        s_player.pool();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ------------------------------------------------------------------ platform
namespace plat {
uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

void* allocLarge(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
    if (!p) ESP_LOGE(TAG, "allocLarge(%u) failed", (unsigned)n);
    return p;
}
void freeLarge(void* p) { heap_caps_free(p); }

void setEngine(float rpm, float thr, float vol) { s_synth.setEngine(rpm, thr, vol); }
void setNoise(float rumble, float skid) { s_synth.setNoise(rumble, skid); }
void playSfx(int id) {
    s_synth.play(id);
    if (id == SFX_CRASH) s_player.vibrator(180);
    else if (id == SFX_BUMP) s_player.vibrator(70);
    else if (id == SFX_HIT_HEAVY || id == SFX_HIT_ROCK) s_player.vibrator(220);
    else if (id == SFX_HIT_WOOD || id == SFX_HIT_METAL || id == SFX_HIT_ICE) s_player.vibrator(140);
    else if (id == SFX_HIT_SOFT) s_player.vibrator(50);
    else if (id == SFX_SCRAPE) s_player.vibrator(80);
}
void playMusic(int song, bool restart) { s_synth.setSong(song, restart); }
void setMusicVolume(float v) { s_synth.setMusicVolume(v); }
unsigned freeInternalKb() { return (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024); }
const char* languageCode() { return akaRuntime.getLanguage(); }
void setLanguageCode(const char* code) { akaRuntime.setLanguage(code); }

bool saveBlob(const void* d, size_t n) {
    FILE* f = fopen(kRecordsPath, "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}
bool loadBlob(void* d, size_t n) {
    FILE* f = fopen(kRecordsPath, "rb");
    if (!f) return false;
    bool ok = fread(d, 1, n, f) == n;
    fclose(f);
    return ok;
}
} // namespace plat

// ------------------------------------------------------------------ input
static plat::Input read_input(const Keys& k) {
    plat::Input in;
    auto map = [](uint32_t raw) {
        uint32_t b = 0;
        if (raw & EXPANDER_KEY_UP)    b |= plat::BTN_UP;
        if (raw & EXPANDER_KEY_DOWN)  b |= plat::BTN_DOWN;
        if (raw & EXPANDER_KEY_LEFT)  b |= plat::BTN_LEFT;
        if (raw & EXPANDER_KEY_RIGHT) b |= plat::BTN_RIGHT;
        if (raw & EXPANDER_KEY_A)     b |= plat::BTN_A;
        if (raw & EXPANDER_KEY_B)     b |= plat::BTN_B;
        if (raw & EXPANDER_KEY_C)     b |= plat::BTN_C;
        if (raw & EXPANDER_KEY_D)     b |= plat::BTN_D;
        if (raw & EXPANDER_KEY_L1)    b |= plat::BTN_L1;
        if (raw & EXPANDER_KEY_R1)    b |= plat::BTN_R1;
        if (raw & EXPANDER_KEY_RUN)   b |= plat::BTN_RUN;
        return b;
    };
    in.held = map(k.raw);
    in.pressed = map(k.pressed);
    // the stick also navigates the menus (D-pad emulation), and steers analog
    in.pressed |= map(g_core.joystick.pressed()) & (plat::BTN_UP | plat::BTN_DOWN | plat::BTN_LEFT | plat::BTN_RIGHT);
    float jx = g_core.joystick.get_x() / 1000.0f;
    float jy = g_core.joystick.get_y() / 1000.0f;
    const float dead = 0.15f;
    in.joyX = jx > dead ? (jx - dead) / (1 - dead) : (jx < -dead ? (jx + dead) / (1 - dead) : 0);
    in.joyY = jy > dead ? (jy - dead) / (1 - dead) : (jy < -dead ? (jy + dead) / (1 - dead) : 0);
    if (in.joyX > 1) in.joyX = 1;
    if (in.joyX < -1) in.joyX = -1;
    return in;
}

// ------------------------------------------------------------------ main
extern "C" void app_main(void) {
    g_core.init();
    gfx.set_backlight_percent(80);
    gfx.set_refresh_rate(60);
    input_init();

    akaRuntime.begin("race3d");
    static const char* const kControls[] = { "CTRL_STEER", "CTRL_GAS", "CTRL_BRAKE", "CTRL_NITRO", "CTRL_PAUSE", nullptr };
    akaRuntime.setControlsKeys(kControls);
    akaRuntime.setCredits("AKA Turbo 3D Grand Prix", "jeancharleslebeau", "MIT", "Gamebuino AKA");
    // "Back to main menu" entry in the system menu, and silence before leaving
    akaRuntime.setGameMenuCallback(game_request_title);
    akaRuntime.setBeforeExitCallback([] {
        s_synth.setEngine(0, 0, 0);
        s_synth.setNoise(0, 0);
        s_synth.setMusicVolume(0);
        plat::netStop();
    });
    mkdir("/sdcard/race3d", 0777);

    gb_ll_audio_set_volume(200);
    s_player.add_track(&s_track, 1.0f);
    s_player.set_master_volume(200);
    xTaskCreatePinnedToCore(audio_task, "race3d_audio", 4096, nullptr, 5, nullptr, 1);

    game_set_framebuffer((uint16_t*)framebuffer);
    game_init();
    ESP_LOGI(TAG, "ready, free SRAM %u, free PSRAM %u", (unsigned)g_core.free_sram(), (unsigned)g_core.free_psram());

    uint32_t last = plat::millis();
    uint32_t fpsT = last, frames = 0;
    while (true) {
        input_poll(g_keys);
        const bool menuWasOpen = akaRuntime.isMenuOpen();
        if (!akaRuntime.update(g_keys)) {          // system menu is open and draws itself
            s_synth.setEngine(0, 0, 0);
            s_synth.setNoise(0, 0);
            last = plat::millis();
            vTaskDelay(pdMS_TO_TICKS(16));
            continue;
        }
        plat::Input in = read_input(g_keys);
        if (menuWasOpen) in = plat::Input();       // the button that closed the menu (A on "Back to menu"...) must not reach the game

        while (!lcd_refresh_completed()) {}       // previous DMA transfer done: safe to draw
        uint32_t now = plat::millis();
        float dt = (now - last) / 1000.0f;
        last = now;
        game_frame(in, dt);
        gfx.update();                              // vsync + DMA to the panel

        if (++frames == 120) {
            ESP_LOGI(TAG, "%.1f fps", frames * 1000.0f / (now - fpsT));
            frames = 0; fpsT = now;
        }
        vTaskDelay(1);                             // let the idle task feed the watchdog
    }
}
