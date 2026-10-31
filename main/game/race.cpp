// race.cpp - physics and AI.
#include "race.h"
#include "gfx.h"
#include <math.h>
#include <algorithm>

using gfx::rgb;

const char* const g_diffNames[DIFF_COUNT] = { "EASY", "NORMAL", "HARD", "EXPERT" };

namespace {

constexpr float ACCEL = MAX_SPEED / 5.0f;
constexpr float BRAKING = MAX_SPEED;
constexpr float DECEL = MAX_SPEED / 5.0f;
constexpr float OFFROAD_DECEL = MAX_SPEED / 2.0f;
constexpr float OFFROAD_LIMIT = MAX_SPEED / 4.0f;
constexpr float CENTRIFUGAL = 0.3f;
constexpr float NITRO_BOOST = 1.28f;

struct DiffProfile {
    float topMin, topMax;     // AI top speed relative to the player car (no nitro)
    float skillMin, skillMax; // cornering / racing line / reflexes
    float mistakes;           // mistakes per second for an unskilled driver
    float rubber;             // catch-up strength
    float accel;
};
const DiffProfile kDiff[DIFF_COUNT] = {
    { 0.78f, 0.87f, 0.15f, 0.45f, 0.30f, 0.06f, 0.70f },   // easy
    { 0.86f, 0.95f, 0.40f, 0.70f, 0.15f, 0.07f, 0.85f },   // normal
    { 0.92f, 0.99f, 0.60f, 0.88f, 0.06f, 0.08f, 0.97f },   // hard
    { 0.97f, 1.04f, 0.82f, 1.00f, 0.02f, 0.10f, 1.06f },   // expert
};

struct Driver { const char* name; uint16_t body, stripe; };
const Driver kDrivers[CAR_COUNT] = {
    { "VIPER", rgb(30, 90, 220),  rgb(255, 214, 0) },
    { "BLAZE", rgb(250, 200, 20), rgb(30, 30, 36) },
    { "NOVA",  rgb(40, 170, 80),  rgb(250, 250, 250) },
    { "RAVEN", rgb(70, 70, 82),   rgb(230, 180, 40) },
    { "ACE",   rgb(240, 240, 244), rgb(220, 30, 40) },
    { "ZED",   rgb(140, 50, 200), rgb(255, 140, 0) },
    { "KAI",   rgb(255, 120, 20), rgb(30, 70, 200) },
    { "YOU",   rgb(220, 24, 36),  rgb(250, 250, 250) },
};

uint32_t s_rng = 0x1234567;
float frand() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return (s_rng & 0xFFFFFF) / 16777216.0f; }
float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

} // namespace

float Race::carZ(const Car& c) const {
    float z = fmodf(c.dist, track.length);
    return z < 0 ? z + track.length : z;
}

int Race::playerLap() const {
    int l = player().lapsDone + 1;
    return l > laps ? laps : l;
}

void Race::setup(int trackId, int diff, int lapCount, bool demoMode) {
    s_rng ^= plat::millis() * 2654435761u;
    if (!s_rng) s_rng = 1;
    track_build(track, trackId);
    difficulty = diff;
    laps = lapCount;
    demo = demoMode;
    autopilot = demoMode;
    raceTime = 0;
    started = false;
    nitro = 1.0f; nitroOn = false; steer = 0;
    skyOffset = nearOffset = 0; shake = 0; offroad = skid = 0;
    gear = 1; rpm = 0;
    pitting = false; pitT = 0; damage = 0; perf = 1; sputter = 0; sputterNext = 2; damageCool = 0; smokeT = 0; spinT = 0;
    const DiffProfile& d = kDiff[diff];

    // AI drivers: random strengths, strongest on pole
    float tops[CAR_COUNT - 1];
    for (int i = 0; i < CAR_COUNT - 1; ++i) tops[i] = d.topMin + (d.topMax - d.topMin) * frand();
    std::sort(tops, tops + CAR_COUNT - 1, [](float a, float b) { return a > b; });

    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = cars[i];
        c = Car();
        c.name = kDrivers[i].name;
        c.livery = { kDrivers[i].body, kDrivers[i].stripe };
        c.isPlayer = (i == playerIdx);
        c.look = i;
        int row = i / 2, col = i % 2;
        c.dist = -SEG_LEN * (3.0f + row * 6.0f) - (col ? SEG_LEN * 2.0f : 0.0f);
        c.x = col ? 0.45f : -0.45f;
        c.targetX = c.x;
        if (!c.isPlayer) {
            c.topFactor = tops[i];
            float rank = (CAR_COUNT - 2 - i) / (float)(CAR_COUNT - 2);   // 1 for pole
            c.skill = d.skillMin + (d.skillMax - d.skillMin) * clampf(rank * 0.6f + frand() * 0.4f, 0, 1);
            c.accelF = d.accel * (0.92f + 0.16f * frand());
            c.laneBias = (frand() - 0.5f) * 0.9f;
            c.launchDelay = 0.15f + (1.0f - c.skill) * 0.6f * frand();
        } else {
            c.topFactor = 0.97f; c.skill = 0.85f; c.accelF = 1.0f; c.laneBias = 0;
            c.launchDelay = 0.1f;
        }
    }
    if (demo) started = true;
    ranking();
}

void Race::update(float dt, const plat::Input& in) {
    ev = RaceEvents();
    if (started) raceTime += dt;
    if (shake > 0) shake -= dt;
    if (spinT > 0) spinT -= dt;
    if (smokeT > 0) smokeT -= dt;
    if (damageCool > 0) damageCool -= dt;

    Car& p = player();
    float oldDist = p.dist;

    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = cars[i];
        if (c.isPlayer && !autopilot) updatePlayer(dt, in);
        else updateAI(c, dt);
    }
    if (autopilot) {   // keep engine/visual state alive for the camera car
        nitroOn = false;
        steer += (clampf(p.steerVis, -1, 1) - steer) * fminf(1, dt * 6);
        offroad = fabsf(p.x) > 1.0f ? 1.0f : 0.0f;
        skid = 0;
    }

    collisions(dt);
    // pit stop: stop in the box of the pit lane (right side, just after the line) to get the engine repaired
    {
        static const float kRepair[DIFF_COUNT] = { 0.34f, 0.26f, 0.20f, 0.16f };     // damage repaired per second
        int pidx = track.indexAt(carZ(p));
        bool inBox = pidx >= PIT_BOX0 && pidx < PIT_BOX1 && p.x > PIT_X0 && p.x < PIT_X1;
        bool on = !autopilot && !demo && started && inBox && speedKmh() < 30.0f && !p.retired && (damage > 0.0f || nitro < 1.0f);
        if (on) {
            if (!pitting) { pitting = true; pitT = 0; if (damage > 0.02f) ev.pitStart = true; }
            pitT += dt;
            nitro = fminf(1.0f, nitro + dt * 0.5f);
            if (damage > 0) { damage -= kRepair[difficulty] * dt; if (damage <= 0) { damage = 0; ev.pitDone = true; } }
            if (damage <= 0 && nitro >= 1.0f) pitting = false;
        } else pitting = false;
    }
    for (auto& c : cars) laps_(c);
    ranking();

    // parallax scrolling follows the curvature
    const Seg& seg = track.at(p.dist);
    float segsMoved = (p.dist - oldDist) / SEG_LEN;
    skyOffset += seg.curve * segsMoved * 0.8f;
    nearOffset += seg.curve * segsMoved * 1.7f;
    while (skyOffset < 0) skyOffset += Track::BG_W;
    while (skyOffset >= Track::BG_W) skyOffset -= Track::BG_W;
    while (nearOffset < 0) nearOffset += Track::BG_W;
    while (nearOffset >= Track::BG_W) nearOffset -= Track::BG_W;

    // gearbox model for the engine sound and HUD
    static const float gearTop[] = { 0.0f, 0.16f, 0.31f, 0.47f, 0.63f, 0.80f, 1.40f };
    float pct = p.speed / MAX_SPEED;
    int g = 1;
    while (g < 6 && pct > gearTop[g]) ++g;
    gear = g;
    float lo = gearTop[g - 1], hi = g == 6 ? 1.05f : gearTop[g];
    float target = 0.22f + 0.78f * clampf((pct - lo) / (hi - lo), 0, 1);
    if (!started) target = (in.held & plat::BTN_A) ? 0.9f : 0.15f;
    rpm += (target - rpm) * fminf(1, dt * 12);
}

void Race::updatePlayer(float dt, const plat::Input& in) {
    Car& p = player();
    if (!started) { p.speed = 0; return; }
    const Seg& seg = track.at(p.dist);
    float pct = p.speed / MAX_SPEED;

    bool gas = in.held & plat::BTN_A;
    bool brake = in.held & (plat::BTN_B | plat::BTN_DOWN);
    bool nitroBtn = in.held & (plat::BTN_C | plat::BTN_R1 | plat::BTN_D);
    float target = 0;
    if (in.held & plat::BTN_LEFT) target = -1;
    else if (in.held & plat::BTN_RIGHT) target = 1;
    else if (fabsf(in.joyX) > 0.05f) target = in.joyX;
    steer += (target - steer) * fminf(1, dt * 9);

    // nitro
    bool wasOn = nitroOn;
    nitroOn = nitroBtn && nitro > 0.02f && gas;
    if (nitroOn) { nitro -= dt / 3.0f; if (nitro < 0) nitro = 0; }
    else nitro = fminf(1.0f, nitro + dt / 28.0f);
    if (nitroOn && !wasOn) plat::playSfx(plat::SFX_NITRO);

    float dx = dt * 2.0f * pct;
    p.x += dx * steer;
    p.x -= dx * pct * seg.curve * CENTRIFUGAL;

    // engine damage: weaker engine, lower top speed, pull to one side, stalling, then death
    float dm = clampf((damage - 0.25f) / 0.75f, 0.0f, 1.0f);
    perf = 1.0f - 0.55f * dm * (0.4f + 0.6f * dm);
    if (p.retired) { gas = false; nitroOn = false; }
    if (damage > 0.6f && !p.retired) {
        sputterNext -= dt;
        if (sputter > 0) sputter -= dt;
        else if (sputterNext <= 0) {
            sputter = 0.12f + 0.25f * frand();
            sputterNext = (2.6f - 2.0f * clampf((damage - 0.6f) / 0.4f, 0, 1)) * (0.6f + 0.8f * frand());
            shake = fmaxf(shake, 0.12f);
        }
        if (sputter > 0) { gas = false; nitroOn = false; }
    } else sputter = 0;
    if (damage > 0.5f && p.speed > MAX_SPEED * 0.1f) p.x += pullDir * (damage - 0.5f) * 0.55f * pct * dt;
    if (nitroOn && damage > 0.8f) nitroOn = false;
    float maxS = MAX_SPEED * perf * (nitroOn ? NITRO_BOOST : 1.0f);
    if (gas) p.speed += ACCEL * dt * (nitroOn ? 1.9f : 1.0f) * (1.0f - 0.55f * pct * pct) * (0.3f + 0.7f * perf);
    else if (brake) p.speed -= BRAKING * dt;
    else p.speed -= DECEL * dt;
    if (gas && brake) p.speed -= BRAKING * dt * 0.5f;

    float ax = fabsf(p.x);
    offroad = ax > 1.12f ? 1.0f : (ax > 1.0f ? 0.35f : 0.0f);
    if (ax > 1.12f && p.speed > OFFROAD_LIMIT) p.speed -= OFFROAD_DECEL * dt;
    if (p.retired) p.speed -= MAX_SPEED * 0.35f * dt;          // dead engine: coast down
    if (p.speed > maxS) p.speed -= MAX_SPEED * (damage > 0.25f ? 0.25f : 0.4f) * dt;   // bleed off after nitro / damage
    if (p.speed < 0) p.speed = 0;
    p.x = clampf(p.x, -2.6f, 2.6f);

    float lateral = fabsf(seg.curve) * pct * pct;
    skid = clampf((lateral - 3.2f) / 2.5f, 0, 1) * (fabsf(steer) > 0.5f ? 1.0f : 0.4f);
    p.braking = brake;
    p.steerVis = steer;
    p.dist += p.speed * dt;
}

void Race::driveAI(Car& c, float dt, float& top) {
    const DiffProfile& d = kDiff[difficulty];
    int n = track.count();
    int idx = track.indexAt(carZ(c));
    int look = 10 + (int)(c.skill * 22);
    float maxC = 0, ahead = 0;
    for (int k = 0; k < look; ++k) {
        float cv = track.segs[(idx + k) % n].curve;
        if (fabsf(cv) > fabsf(maxC)) maxC = cv;
        if (k < 14) ahead += cv;
    }
    float corner = 1.0f - fabsf(maxC) / 6.0f * (0.30f - 0.22f * c.skill);
    top = MAX_SPEED * c.topFactor * corner;

    if (!demo && !c.isPlayer && !player().finished) {
        float gap = player().dist - c.dist;   // > 0 : AI is behind the player
        float rb = clampf(gap / (track.length * 0.12f), -1, 1);
        top *= 1.0f + rb * d.rubber;
    }

    // occasional mistakes (less skill -> more mistakes)
    if (c.wobble <= 0 && !c.isPlayer && frand() < d.mistakes * (1.2f - c.skill) * dt) {
        c.wobble = 0.6f + frand() * 0.8f;
        c.wobbleDir = frand() < 0.5f ? -1.0f : 1.0f;
    }

    // preferred racing line: inside of the coming curve
    float tx = clampf(ahead / 45.0f, -0.65f, 0.65f) * c.skill + c.laneBias * (1.0f - 0.5f * c.skill);

    // traffic: find the nearest car ahead in our way
    float bestDz = 1e9f; const Car* block = nullptr;
    float range = SEG_LEN * (8 + c.skill * 14);
    for (auto& o : cars) {
        if (&o == &c) continue;
        float dz = carZ(o) - carZ(c);
        if (dz < -track.length / 2) dz += track.length;
        if (dz > track.length / 2) dz -= track.length;
        if (dz <= 0 || dz > range || dz > bestDz) continue;
        if (o.speed > c.speed * 1.02f) continue;
        if (!overlap(c.x, CAR_NORM_W, o.x, CAR_NORM_W, 1.5f)) continue;
        bestDz = dz; block = &o;
    }
    if (block) {
        float dir;
        if (block->x > 0.4f) dir = -1;
        else if (block->x < -0.4f) dir = 1;
        else dir = c.x >= block->x ? 1.0f : -1.0f;
        tx = block->x + dir * CAR_NORM_W * 1.7f;
        if (bestDz < SEG_LEN * 2.5f && overlap(c.x, CAR_NORM_W, block->x, CAR_NORM_W, 1.1f))
            top = fminf(top, block->speed * 0.97f);
    }
    if (c.wobble > 0) {
        c.wobble -= dt;
        tx += c.wobbleDir * 0.55f;
        top *= 0.88f;
    }
    c.targetX = clampf(tx, -0.85f, 0.85f);

    float rate = (0.5f + 0.9f * c.skill) * clampf(c.speed / (MAX_SPEED * 0.3f), 0.2f, 1.0f);
    float mv = clampf(c.targetX - c.x, -rate * dt, rate * dt);
    c.x += mv;
    float vis = dt > 0 ? mv / (rate * dt + 1e-6f) : 0;
    c.steerVis += (vis + maxC * 0.08f - c.steerVis) * fminf(1, dt * 6);
}

void Race::updateAI(Car& c, float dt) {
    if (c.retired) {   // the player's car after engine failure
        c.speed = fmaxf(0.0f, c.speed - MAX_SPEED * 0.35f * dt);
        c.dist += c.speed * dt;
        c.braking = true;
        return;
    }
    if (!started || raceTime < c.launchDelay) { c.speed = 0; c.braking = true; return; }
    float top;
    driveAI(c, dt, top);
    float pct = c.speed / MAX_SPEED;
    if (c.speed < top) {
        c.speed += ACCEL * c.accelF * dt * (1.0f - 0.5f * pct * pct);
        if (c.speed > top) c.speed = top;
        c.braking = false;
    } else {
        float before = c.speed;
        c.speed = fmaxf(top, c.speed - MAX_SPEED * 0.6f * dt);
        c.braking = (before - top) > MAX_SPEED * 0.03f;
    }
    if (fabsf(c.x) > 1.1f && c.speed > OFFROAD_LIMIT) c.speed -= OFFROAD_DECEL * dt;
    c.dist += c.speed * dt;
}

void Race::addDamage(float d) {
    if (autopilot || demo || d <= 0) return;
    static const float kDiffScale[DIFF_COUNT] = { 0.7f, 1.0f, 1.15f, 1.3f };
    float before = damage;
    if (before <= 0 && d > 0) pullDir = frand() < 0.5f ? -1 : 1;
    damage = clampf(damage + d * kDiffScale[difficulty], 0.0f, 1.0f);
    if (damage >= 1.0f && !player().retired) { player().retired = true; ev.retired = true; }
}

static HitKind hit_kind(int spr) {
    switch (spr) {
    case SPR_BUSH: case SPR_FLOWERS: case SPR_DUNEGRASS: case SPR_HAYBALE: case SPR_SNOWMAN: case SPR_PARASOL: return HIT_SOFT;
    case SPR_PALM: case SPR_PINE: case SPR_TREE: case SPR_FENCE: case SPR_CRATES: case SPR_HUT: return HIT_WOOD;
    case SPR_ROCK: return HIT_ROCK;
    case SPR_ICE: return HIT_ICE;
    case SPR_BUILDING_A: case SPR_BUILDING_B: case SPR_BUILDING_C: case SPR_WAREHOUSE: case SPR_CONTAINER: case SPR_CONTAINER2:
    case SPR_CRANE: case SPR_LIGHTHOUSE: case SPR_GANTRY: return HIT_HEAVY;
    default: return HIT_METAL;      // posts, lamps, boards, chevrons, barriers
    }
}

void Race::collisions(float dt) {
    (void)dt;
    Car& p = player();
    static float crashCooldown = 0, sideCooldown = 0;
    if (crashCooldown > 0) crashCooldown -= dt;
    if (sideCooldown > 0) sideCooldown -= dt;

    // roadside objects
    if (fabsf(p.x) > 1.0f && !autopilot) {
        int n = track.count();
        int idx = track.indexAt(carZ(p));
        for (int k = 0; k <= 1; ++k) {
            const Seg& s = track.segs[(idx + k) % n];
            for (int j = 0; j < s.propCount; ++j) {
                const Prop& pr = track.props[s.propFirst + j];
                const Sprite& sp = g_sprites[pr.spr];
                if (sp.hitW <= 0) continue;
                float w = sp.worldW / ROAD_W;
                float centre = pr.offset + (pr.offset > 0 ? w / 2 : -w / 2);
                if (overlap(p.x, CAR_NORM_W, centre, w * sp.hitW)) {
                    HitKind kind = hit_kind(pr.spr);
                    float cap = MAX_SPEED / 7.0f, push = 0.12f, shk = 0.5f, cool = 0.6f;
                    switch (kind) {
                    case HIT_SOFT:  cap = MAX_SPEED * 0.55f; push = 0.03f; shk = 0.18f; cool = 0.3f; break;
                    case HIT_WOOD:  cap = MAX_SPEED / 7.0f;  push = 0.12f; shk = 0.5f;  break;
                    case HIT_ROCK:  cap = MAX_SPEED / 8.5f;  push = 0.14f; shk = 0.75f; break;
                    case HIT_METAL: cap = MAX_SPEED / 6.0f;  push = 0.10f; shk = 0.45f; break;
                    case HIT_ICE:   cap = MAX_SPEED / 5.5f;  push = 0.17f; shk = 0.4f;  break;
                    case HIT_HEAVY: cap = MAX_SPEED / 9.0f;  push = 0.16f; shk = 0.9f;  break;
                    default: break;
                    }
                    if (crashCooldown <= 0 && p.speed > MAX_SPEED * 0.12f) {
                        float power = clampf(p.speed / MAX_SPEED, 0.0f, 1.0f);
                        ev.crash = true;
                        ev.hit = kind;
                        ev.hitSide = pr.offset > 0 ? 1 : -1;
                        ev.hitSpr = pr.spr;
                        ev.hitPower = power;
                        crashCooldown = cool;
                        shake = shk * (0.55f + 0.45f * power);
                        if (kind == HIT_SOFT) p.speed *= 0.72f; else p.speed = fminf(p.speed, cap);
                        p.x += (p.x > 0 ? -1.0f : 1.0f) * push;
                        spinT = kind == HIT_SOFT ? 0.0f : 0.55f;
                        spinDir = p.x > 0 ? 1 : -1;     // the car yaws away from the obstacle
                        {
                            static const float kDmg[] = { 0, 0.01f, 0.09f, 0.17f, 0.12f, 0.09f, 0.26f };
                            addDamage(kDmg[kind] * (0.35f + 0.65f * power * power));
                        }
                        if (kind == HIT_METAL || kind == HIT_ROCK || kind == HIT_HEAVY) smokeT = 0.8f + 1.6f * power;
                    } else if (kind != HIT_SOFT && p.speed > cap) p.speed = cap;
                }
            }
        }
    }

    // car to car
    for (int i = 0; i < CAR_COUNT; ++i) {
        for (int j = i + 1; j < CAR_COUNT; ++j) {
            Car& a = cars[i];
            Car& b = cars[j];
            float dz = carZ(b) - carZ(a);
            if (dz < -track.length / 2) dz += track.length;
            if (dz > track.length / 2) dz -= track.length;
            if (fabsf(dz) > SEG_LEN * 0.9f) continue;
            if (!overlap(a.x, CAR_NORM_W, b.x, CAR_NORM_W, 0.9f)) continue;
            Car& front = dz > 0 ? b : a;
            Car& back = dz > 0 ? a : b;
            float gap = fabsf(dz);
            float lat = fabsf(a.x - b.x) / CAR_NORM_W;        // 0 = dead centre behind, ~0.9 = fenders just touching
            bool involvesPlayer = back.isPlayer || front.isPlayer;
            const Car& other = (back.isPlayer ? front : back);
            if (back.speed > front.speed) {
                float vb = back.speed;
                back.speed = front.speed * 0.85f;
                front.speed = fminf(front.speed + (vb - front.speed) * 0.3f, MAX_SPEED * 1.1f);
                back.dist -= (SEG_LEN * 0.9f - gap);
                if (involvesPlayer) {
                    float power = clampf((vb - front.speed) / (MAX_SPEED * 0.35f), 0.2f, 1.0f);
                    bool glancing = lat > 0.45f;
                    ev.bump = !glancing;
                    ev.hit = glancing ? HIT_CAR_SIDE : HIT_CAR_REAR;
                    ev.hitSide = other.x >= player().x ? 1 : -1;
                    ev.hitPower = power;
                    ev.hitColor = other.livery.body;
                    shake = fmaxf(shake, glancing ? 0.18f : 0.3f);
                    if (!glancing) { spinT = 0.35f; spinDir = ev.hitSide > 0 ? -1 : 1; }
                    if (damageCool <= 0) {
                        damageCool = 0.5f;
                        addDamage((glancing ? 0.015f : (back.isPlayer ? 0.06f : 0.035f)) * power);
                    }
                }
            } else if (involvesPlayer && lat > 0.35f && gap < SEG_LEN * 0.55f && sideCooldown <= 0) {
                // alongside: paint rubbing
                ev.hit = HIT_CAR_SIDE;
                ev.hitSide = other.x >= player().x ? 1 : -1;
                ev.hitPower = 0.35f;
                ev.hitColor = other.livery.body;
                sideCooldown = 0.45f;
            }
            float push = (a.x < b.x ? -1.0f : 1.0f) * 0.035f;
            a.x += push; b.x -= push;
        }
    }
}

void Race::laps_(Car& c) {
    int done = c.dist >= 0 ? (int)(c.dist / track.length) : 0;
    if (done > c.lapsDone) {
        c.lapsDone = done;
        c.lastLap = raceTime - c.lapStart;
        c.lapStart = raceTime;
        bool best = (c.bestLap == 0 || c.lastLap < c.bestLap);
        if (best) c.bestLap = c.lastLap;
        if (c.isPlayer && !demo) {
            ev.lap = true;
            ev.bestLap = best && done > 1;
            if (done == laps - 1) ev.finalLap = true;
        }
    }
    if (!c.finished && !demo && done >= laps) {
        c.finished = true;
        c.finishTime = raceTime;
        if (c.isPlayer) ev.finished = true;
    }
}

void Race::ranking() {
    int order[CAR_COUNT];
    for (int i = 0; i < CAR_COUNT; ++i) order[i] = i;
    std::sort(order, order + CAR_COUNT, [this](int ia, int ib) {
        const Car& a = cars[ia]; const Car& b = cars[ib];
        if (a.finished != b.finished) return a.finished;
        if (a.finished) return a.finishTime < b.finishTime;
        return a.dist > b.dist;
    });
    for (int k = 0; k < CAR_COUNT; ++k) cars[order[k]].place = k + 1;
}

void Race::estimateUnfinished() {
    for (auto& c : cars) {
        if (c.finished) continue;
        float remain = laps * track.length - c.dist;
        float v = fmaxf(c.speed, MAX_SPEED * 0.6f);
        c.finishTime = c.retired ? raceTime + 99999.0f : raceTime + remain / v;
        c.finished = true;
    }
    ranking();
}
