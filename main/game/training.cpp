// training.cpp - training circuit (a gentle lap with one sharp bend and a long straight, the pit lane at the end of lap 1)
// and the lessons the coach gives. Lap 1: accelerate, steer, brake, nitro, pit stop. Lap 2: free run with the chevrons.
#include "training.h"
#include <math.h>

const TrackDef g_trainingDef = { "TRAINING", THEME_COAST, 0, 777, 55, R"TRK(
straight LONG
curve LONG EASY
curve LONG -EASY
straight MEDIUM
curve LONG HARD
straight LONG
curve LONG MED
curve MEDIUM -MED
straight LONG
)TRK" };

namespace training {
namespace {
struct Lesson { const char* key; float pct; };      // lap 1 position (fraction of the lap) where the lesson starts
const Lesson kLessons[LESSONS] = {
    { "HOLD A TO ACCELERATE.",                      0.00f },
    { "STEER WITH LEFT AND RIGHT. STAY ON THE TARMAC.", 0.12f },
    { "SHARP BEND! RELEASE A AND BRAKE WITH B.",   0.36f },
    { "HOLD A AND THE NITRO BUTTON FOR A BOOST.",  0.58f },
    { "ENGINE DAMAGED. TAKE THE PIT LANE ON THE RIGHT.", 0.88f },
    { "STOP IN THE BAY. WAIT FOR THE GREEN LIGHT.", 2.0f },        // when the car reaches the apron
    { "LAP TWO. USE THE CHEVRONS TO ANTICIPATE BENDS.", 2.0f },    // lap 2, shortly after the line
    { "TRAINING COMPLETE. YOU ARE READY TO RACE!", 2.0f },         // chequered flag
};
bool s_done[LESSONS];
}

const char* text(int lesson) { return kLessons[lesson].key; }
void reset() { for (bool& d : s_done) d = false; }

void update(Race& r, void (*say)(const char*, int)) {
    if (!r.started || r.player().retired) return;
    const Car& p = r.player();
    float L = r.track.length > 1 ? r.track.length : 1.0f;
    float frac = p.dist <= 0 ? 0.0f : fmodf(p.dist, L) / L;         // on the grid (dist < 0) the lap has not started
    int lap = p.lapsDone;                                           // 0 = first lap
    auto fire = [&](int i) { if (!s_done[i]) { s_done[i] = true; say(kLessons[i].key, i); } };
    if (lap == 0) {
        for (int i = 0; i < 5; ++i) {
            if (frac < kLessons[i].pct) break;
            if (i == 3 && r.nitro < 0.9f) continue;                  // the boost lesson needs a full tank
            if (i == 4 && !s_done[4]) r.damage = fmaxf(r.damage, 0.45f);   // something to repair
            fire(i);
        }
        if (r.onApron) fire(5);
    } else {
        if (frac > 0.12f) fire(6);
    }
}

void finished(void (*say)(const char*, int)) { if (!s_done[7]) { s_done[7] = true; say(kLessons[7].key, 7); } }
}
