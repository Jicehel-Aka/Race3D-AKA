// scen_art.h - roadside scenery pictures (see scen_art.cpp). Row = theme, 6 per theme.
#pragma once
#include <stdint.h>
constexpr int SCENE_COUNT = 36;
struct SceneArt { uint8_t w, h; float worldW, hitW; uint8_t hit; uint16_t emissive; const uint16_t* pal; const uint8_t* px; };
extern const SceneArt kScene[SCENE_COUNT];
enum SceneId {
  SC_PARASOL2 = 0, SC_LIFEGUARD, SC_LIGHTHOUSE2, SC_SURFRACK, SC_FLAMINGO, SC_SAILBOAT, SC_FIR, SC_CHALET, SC_COW, SC_LOGS, SC_CROSS, SC_PYLON, SC_TOWER, SC_NEONSIGN, SC_LAMP2, SC_BUSSTOP, SC_TAXI, SC_TRAFFICLIGHT, SC_PALM2, SC_HIBISCUS, SC_TIKIHUT, SC_IDOL, SC_BANANA, SC_TORCH, SC_SNOWFIR, SC_CRYSTAL, SC_SNOWMAN2, SC_SNOWROCK, SC_IGLOO, SC_SNOWPOLE, SC_BLUECONT, SC_REDCONT, SC_CRANE2, SC_BARRELS, SC_BOLLARD, SC_FISHBOAT
};
