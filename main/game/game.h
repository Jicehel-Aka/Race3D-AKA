// game.h - top level state machine (title, countdown, race, results).
#pragma once
#include "platform.h"

// Back to the title screen (system menu "Back to main menu").
void game_request_title();
void game_set_framebuffer(uint16_t* fb);   // 320x240, must be set before game_init()
void game_init();
// Simulates and draws one frame into gfx::fb. dt in seconds.
void game_frame(const plat::Input& in, float dt);
// True when the game wants the engine sound muted (menus).
bool game_paused();

// Test hooks for the PC preview build.
class Race;
Race* game_debug_race();
void game_debug_select(int track, int difficulty, int lapsIndex);
bool game_is_title();   // true on the title screen (PC: Esc quits there)
