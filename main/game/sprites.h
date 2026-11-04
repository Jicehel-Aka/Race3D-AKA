// sprites.h - procedurally generated, palette-indexed pixel art.
// Index 0 is transparent, index 255 darkens what is behind (soft shadow).
#pragma once
#include <stdint.h>

enum SpriteId : uint8_t {
    SPR_CAR, SPR_CAR_LEFT, SPR_CAR_RIGHT,
    SPR_PALM, SPR_PINE, SPR_TREE, SPR_BUSH, SPR_ROCK,
    SPR_BOARD_A, SPR_BOARD_B, SPR_BOARD_C,
    SPR_CHEVRON, SPR_LAMP,
    SPR_BUILDING_A, SPR_BUILDING_B, SPR_BUILDING_C,
    SPR_GANTRY,
    // added with the Palm Bay / Glacier Run / Night Harbor looks
    SPR_PARASOL, SPR_HUT, SPR_LIGHTHOUSE, SPR_ICE, SPR_SNOWMAN,
    SPR_CONTAINER, SPR_CONTAINER2, SPR_CRANE, SPR_WAREHOUSE,
    SPR_BOARD_D, SPR_BOARD_E, SPR_BOARD_F,
    // roadside dressing
    SPR_CRATES, SPR_BARRIER, SPR_DUNEGRASS, SPR_FENCE, SPR_HAYBALE, SPR_FLOWERS, SPR_POLE,
    SPR_CAR_LEFT3Q, SPR_CAR_RIGHT3Q,   // three-quarter rear views (strong turns, spin-outs)
    SPR_TECH0, SPR_TECH1, SPR_TECH2, SPR_TECH3, SPR_TECH4, SPR_TECH5,   // pit crew technician frames (tech_art.h)
    SPR_COUNT
};

struct Sprite {
    int16_t w = 0, h = 0;
    uint8_t* px = nullptr;
    float worldW = 1000;     // width in world units (road half width = 2000)
    float hitW = 0.5f;       // fraction of worldW that is solid
    uint16_t emissive = 0;   // bitmask of palette indices that ignore ambient light
    int16_t anchorX = 0;     // cars: x of the rear-face centre inside the sprite (0 = centred)
    int16_t lightL = 0, lightR = 0;   // cars: x of the tail lights (0 = default 20 / 92)
};

extern Sprite g_sprites[SPR_COUNT];

void sprites_init();

// Palette with 16 entries for a scenery sprite in the given theme (0..5).
const uint16_t* sprite_palette(int id, int theme);

// Car livery palette.
struct Livery { uint16_t body, stripe; };
void car_palette(uint16_t out[16], const Livery& l, bool braking);

// Car palette index of the tail lights (to light them up)
constexpr int CAR_TAIL = 9, CAR_TAIL_B = 10;

// Scaled blit. (x, y) top-left, (dw, dh) destination size, rows >= clipY hidden.
// True colour car pictures (RGB565 with a colour key) with 2 box-filtered mip levels; bilinear blit.
void car_art_init();
bool car_art_ready();
// dir -1/0/+1: lean of the upper body (sheared turn frame), amp: shear in 112-px sprite units
// tq: use the three-quarter picture (nose to the left; flip mirrors it). dw is the width of the REAR view; the 3/4 picture is drawn wider.
void car_shadow(int driver, int stage, bool flip, int x, int y, int dw, int dh, int clipY);
void blit_car(int driver, int stage, bool flip, int x, int y, int dw, int dh, int clipY, int ambient, uint16_t fogc, int fog);

void blit(const Sprite& s, const uint16_t* pal, int x, int y, int dw, int dh, int clipY, bool flip);
