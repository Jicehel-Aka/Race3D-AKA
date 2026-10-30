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
};

struct RaceEvents {     // things the renderer / game wants to react to this frame
    bool crash = false, bump = false, lap = false, bestLap = false, finalLap = false, finished = false;
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
