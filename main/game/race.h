// race.h - race simulation: player physics, AI drivers, laps and positions.
#pragma once
#include <stdint.h>
#include "track.h"
#include "sprites.h"
#include "platform.h"

constexpr int   CAR_COUNT = 8;
constexpr float MAX_SPEED = SEG_LEN * 60.0f;     // 12000 units/s ~ 240 km/h
// pit lane v1.2: separate apron on the right of the road, behind a concrete wall, three bays (segment indices)
// The apron starts BEFORE the start line (PIT_PRE segments at the end of the lap, numbered -PIT_PRE..-1 by pitIdx) and the wall begins
// only at the second bay, so there is a long open entry (about 30 segments) to slip into the pit lane.
constexpr int   PIT_PRE = 12, PIT_ENTRY = -PIT_PRE, PIT_WALL_FIRST = 19, PIT_WALL_END = 38, PIT_END = 44;
inline int pitIdx(int idx, int n) { return idx >= n - PIT_PRE ? idx - n : idx; }      // segment index as seen by the pit lane
inline float pitTaper(int p) {                                                        // 0 where the apron starts .. 1 at full width
    if (p < PIT_WALL_FIRST) return (float)(p - PIT_ENTRY) / (PIT_WALL_FIRST - PIT_ENTRY);
    if (p > PIT_WALL_END) return 1.0f - (float)(p - PIT_WALL_END) / (PIT_END - PIT_WALL_END);
    return 1.0f;
}
constexpr float PIT_WALL_X = 1.04f, PIT_STRIP = 0.80f, PIT_LANE_X1 = 2.62f;           // wall centre / far edge of the apron (road edge = 1.0)
inline float pitOuter(float taper) { return 1.12f + 1.60f * taper; }                // outer edge of the pit apron
inline float pitInner(int p) { return p < PIT_WALL_FIRST ? 1.07f : 1.05f + PIT_STRIP * pitTaper(p); }   // service lane starts behind the technicians' strip
constexpr int   PIT_BAY_NITRO0 = 11, PIT_BAY_NITRO1 = 18;           // 1: N2O express, rolling (< 95 km/h)
constexpr int   PIT_BAY_CHASSIS0 = 19, PIT_BAY_CHASSIS1 = 26;       // 2: tyres & tracking, slow (< 38 km/h)
constexpr int   PIT_BAY_ENGINE0 = 27, PIT_BAY_ENGINE1 = 36;         // 3: full engine garage (< 30 km/h)
constexpr float PIT_LIMIT_KMH = 85.0f;                              // speed limiter on the apron
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
    int look = 0;          // which imported car picture this driver uses
    bool retired = false;  // engine dead: the car coasts to a stop and is out of the race
    // online races: this car is driven by another player / the server, we only follow its last known state
    bool remote = false;
    float netDist = 0, netX = 0, netSpeed = 0, netSteer = 0, netAge = 0;
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
    bool pitStart = false, pitDone = false, retired = false, crash = false, bump = false, lap = false, bestLap = false, finalLap = false, finished = false;
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
    bool  pitting = false;                 // being serviced in a bay
    float pitT = 0;
    int   activeBay = 0;                   // 0 none, 1 N2O, 2 tyres & tracking, 3 engine garage
    int   lollipop = 0;                    // 0 hidden, 1 STOP, 2 READY, 3 GO
    float lollipopT = 0, launchBoostT = 0;
    // pit crews of the two serviced bays (0 = tyres & tracking, 1 = engine garage): wait outside, walk to the stopped car, work, walk away
    enum CrewPhase : uint8_t { CREW_IDLE, CREW_IN, CREW_WORK, CREW_OUT };
    struct Crew { CrewPhase phase = CREW_IDLE; float progress = 0, workT = 0, workReq = 2, anchorZ = 0, anchorX = 2.1f, startDamage = 0, startNitro = 1; bool serviced = false; };
    Crew  crew[2];
    bool  onApron = false;                 // player is on the pit apron (speed limiter, no grass penalty)
    bool  inPitZone(const Car& c) const;
    int   pullDir = 1;
    void  addDamage(float d);
    float offroad = 0, skid = 0;
    int gear = 1;
    float rpm = 0;
    float gasGrace = 0;              // seconds left in which the nitro button keeps the throttle after A was lifted
    bool training = false;           // the training circuit (lessons + slow rivals)
    float shiftT = 0;                      // > 0 while the gearbox changes up: engine torque cut
    float pitch = 0;                       // body pitch, -1 squat under power .. +1 dive under braking (visual)
    float cornerWarn(int* dir) const;      // 0..1: how much too fast the player is for the sharpest bend ahead (dir = -1 left / +1 right)
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
    void updateRemote(Car& c, float dt);
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
