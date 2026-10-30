// render.h - pseudo-3D world renderer, HUD and particle effects.
#pragma once
#include "race.h"

void render_world(const Race& r, float time);
void render_hud(const Race& r, float time);
void render_minimap(const Race& r, int x, int y, float time);
void render_tower(const Race& r, int x, int y);

// particles (screen space)
void fx_update(const Race& r, float dt);
void fx_draw();
void fx_clear();
void fx_burst(int x, int y, int n, uint16_t c, float speed, float life);

void format_time(char* out, float t);   // 1'23"45
