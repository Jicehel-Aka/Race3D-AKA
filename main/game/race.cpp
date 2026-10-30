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

    float maxS = MAX_SPEED * (nitroOn ? NITRO_BOOST : 1.0f);
    if (gas) p.speed += ACCEL * dt * (nitroOn ? 1.9f : 1.0f) * (1.0f - 0.55f * pct * pct);
    else if (brake) p.speed -= BRAKING * dt;
    else p.speed -= DECEL * dt;
    if (gas && brake) p.speed -= BRAKING * dt * 0.5f;

    float ax = fabsf(p.x);
    offroad = ax > 1.12f ? 1.0f : (ax > 1.0f ? 0.35f : 0.0f);
    if (ax > 1.12f && p.speed > OFFROAD_LIMIT) p.speed -= OFFROAD_DECEL * dt;
    if (p.speed > maxS) p.speed -= MAX_SPEED * 0.4f * dt;   // bleed off after nitro
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

void Race::collisions(float dt) {
    (void)dt;
    Car& p = player();
    static float crashCooldown = 0;
    if (crashCooldown > 0) crashCooldown -= dt;

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
                    if (crashCooldown <= 0 && p.speed > MAX_SPEED * 0.12f) {
                        ev.crash = true;
                        crashCooldown = 0.6f;
                        shake = 0.5f;
                        p.speed = MAX_SPEED / 7.0f;
                        p.x += (p.x > 0 ? -1.0f : 1.0f) * 0.12f;
                    } else if (p.speed > MAX_SPEED / 7.0f) p.speed = MAX_SPEED / 7.0f;
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
            if (back.speed > front.speed) {
                float vb = back.speed;
                back.speed = front.speed * 0.85f;
                front.speed = fminf(front.speed + (vb - front.speed) * 0.3f, MAX_SPEED * 1.1f);
                back.dist -= (SEG_LEN * 0.9f - gap);
                if (back.isPlayer || front.isPlayer) {
                    ev.bump = true;
                    shake = fmaxf(shake, 0.25f);
                }
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
        c.finishTime = raceTime + remain / v;
        c.finished = true;
    }
    ranking();
}
