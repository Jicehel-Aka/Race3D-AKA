// race.h - race simulation: player physics, AI drivers, laps and positions.
#pragma once
#include <stdint.h>
#include "track.h"
#include "sprites.h"
#include "platform.h"

constexpr int   CAR_COUNT = 8;
constexpr float MAX_SPEED = SEG_LEN * 60.0f;     // 12000 units/s ~ 240 km/h
constexpr float CAM_HEIGHT = 1000.0f;
constexpr float CAM_DEPTH = 0.8391f;             // 1 / tan(100deg / 2)
constexpr float PLAYER_Z = CAM_HEIGHT * CAM_DEPTH;
constexpr float CAR_NORM_W = 700.0f / ROAD_W;    // car width in road units

enum Difficulty { DIFF_EASY = 0, DIFF_NORMAL, DIFF_HARD, DIFF_EXPERT, DIFF_COUNT };
extern const char* const g_diffNames[DIFF_COUNT];

struct Car {
    float dist = 0;        // distance driven since the start line (negative on the grid)
    float x = 0;           // lateral position, road edges at -1 / +1
    float speed = 0;
    // driver profile
    const char* name = "";
    Livery livery{};
    bool isPlayer = false;
    float topFactor = 1, skill = 0.5f, accelF = 1, laneBias = 0, launchDelay = 0;
    // AI state
    float targetX = 0, wobble = 0, wobbleDir = 0, steerVis = 0;
    bool braking = false;
    // race state
    int lapsDone = 0;
    float lapStart = 0, lastLap = 0, bestLap = 0;
    bool finished = false;
    float finishTime = 0;
    int place = 0;
    bool retired = false;  // engine dead: the car coasts to a stop and is out of the race
};

// what the player hit: drives the particles, the sound and the physics of the impact
enum HitKind : uint8_t {
    HIT_NONE = 0,
    HIT_SOFT,        // bushes, flowers, hay, snow men, parasols: the car ploughs through
    HIT_WOOD,        // trunks, fences, crates, huts
    HIT_ROCK,
    HIT_METAL,       // posts, billboards, barriers, lamps
    HIT_ICE,
    HIT_HEAVY,       // buildings, containers, cranes, lighthouse
    HIT_CAR_REAR,    // shunting another car
    HIT_CAR_SIDE     // side-swiping another car
};

struct RaceEvents {     // things the renderer / game wants to react to this frame
    bool retired = false, crash = false, bump = false, lap = false, bestLap = false, finalLap = false, finished = false;
    uint8_t hit = HIT_NONE;     // set together with crash / bump
    int8_t hitSide = 0;         // -1 / +1 : side of the player where the contact happened
    uint8_t hitSpr = 0;         // sprite id of the obstacle (scenery hits)
    float hitPower = 0;         // 0..1 impact strength
    uint16_t hitColor = 0;      // paint of the other car (car hits)
};

class Race {
public:
    Track track;
    Car cars[CAR_COUNT];
    int playerIdx = CAR_COUNT - 1;
    int difficulty = DIFF_NORMAL;
    int laps = 3;
    bool demo = false;          // attract mode: everybody is driven by the AI
    bool autopilot = false;     // player car driven by the AI (demo / after finish)

    float raceTime = 0;
    bool started = false;       // countdown over
    float nitro = 1.0f;         // gauge 0..1
    bool nitroOn = false;
    float steer = 0;            // smoothed player steering, -1..1
    float skyOffset = 0, nearOffset = 0;
    float shake = 0;
    float spinT = 0; int spinDir = 0;     // brief yaw of the player's car after an impact
    float smokeT = 0;                      // damage smoke after a hard hit
    // engine damage: 0 = mint, 1 = dead. Grows with every impact; past ~0.25 the car loses power and top speed,
    // past ~0.5 it pulls to one side, past ~0.6 the engine cuts out now and then, at 1 it stops for good.
    float damage = 0;
    float perf = 1;                        // power factor derived from the damage (1 .. ~0.4)
    float sputter = 0;                     // > 0 while the engine is cutting out
    float sputterNext = 2;
    float damageCool = 0;
    int   pullDir = 1;
    void  addDamage(float d);
    float offroad = 0, skid = 0;
    int gear = 1;
    float rpm = 0;
    RaceEvents ev;

    void setup(int trackId, int diff, int lapCount, bool demoMode);
    void start() { started = true; raceTime = 0; for (auto& c : cars) c.lapStart = 0; }
    void update(float dt, const plat::Input& in);

    Car& player() { return cars[playerIdx]; }
    const Car& player() const { return cars[playerIdx]; }
    float cameraZ() const { return player().dist - PLAYER_Z; }
    float carZ(const Car& c) const;          // wrapped position on the lap
    float speedKmh() const { return player().speed / MAX_SPEED * 240.0f; }
    int   playerLap() const;                 // 1-based, clamped to laps
    void  estimateUnfinished();

private:
    void updatePlayer(float dt, const plat::Input& in);
    void updateAI(Car& c, float dt);
    void driveAI(Car& c, float dt, float& targetSpeed);
    void collisions(float dt);
    void laps_(Car& c);
    void ranking();
};

// overlap test in road units
inline bool overlap(float x1, float w1, float x2, float w2, float percent = 1.0f) {
    float half = percent / 2;
    float min1 = x1 - w1 * half, max1 = x1 + w1 * half;
    float min2 = x2 - w2 * half, max2 = x2 + w2 * half;
    return !((max1 < min2) || (min1 > max2));
}
