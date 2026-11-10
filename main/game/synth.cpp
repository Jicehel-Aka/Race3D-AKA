#include "synth.h"
#include <string.h>
#include "platform.h"
#include "music.h"
#include <math.h>

// sound effect programs ---------------------------------------------------
#define S(f0, f1, v, d, w) { f0, f1, v, d, w }
static const Synth::Step kBeepLow[] = { S(440, 440, 0.45f, 0.28f, 0) };
static const Synth::Step kBeepHigh[] = { S(880, 880, 0.5f, 0.7f, 0) };
static const Synth::Step kMove[] = { S(1100, 1500, 0.25f, 0.04f, 0) };
static const Synth::Step kOk[] = { S(660, 660, 0.3f, 0.06f, 0), S(990, 990, 0.3f, 0.12f, 0) };
static const Synth::Step kCrash[] = { S(900, 120, 0.9f, 0.45f, 1), S(120, 40, 0.5f, 0.3f, 0) };
static const Synth::Step kBump[] = { S(700, 200, 0.6f, 0.10f, 1), S(130, 70, 0.5f, 0.14f, 0) };
static const Synth::Step kBumpB[] = { S(180, 80, 0.7f, 0.12f, 2) };
// impacts by material: a main program and a layered second one
static const Synth::Step kSoftA[] = { S(180, 90, 0.35f, 0.16f, 1), S(110, 60, 0.22f, 0.22f, 1) };
static const Synth::Step kSoftB[] = { S(140, 70, 0.30f, 0.10f, 2) };
static const Synth::Step kWoodA[] = { S(1800, 600, 0.5f, 0.03f, 1), S(330, 300, 0.35f, 0.04f, 0), S(90, 60, 0.5f, 0.18f, 1), S(250, 230, 0.3f, 0.05f, 0) };
static const Synth::Step kWoodB[] = { S(200, 70, 0.7f, 0.14f, 2), S(70, 45, 0.5f, 0.2f, 0) };
static const Synth::Step kRockA[] = { S(2400, 400, 0.55f, 0.05f, 1), S(70, 40, 0.6f, 0.35f, 1) };
static const Synth::Step kRockB[] = { S(110, 35, 0.8f, 0.30f, 0), S(60, 30, 0.7f, 0.35f, 2) };
static const Synth::Step kMetalA[] = { S(1250, 1180, 0.5f, 0.05f, 0), S(900, 880, 0.45f, 0.08f, 0), S(1320, 1300, 0.35f, 0.12f, 0) };
static const Synth::Step kMetalB[] = { S(3000, 1200, 0.5f, 0.06f, 1), S(500, 200, 0.4f, 0.2f, 1) };
static const Synth::Step kIceA[] = { S(3600, 1800, 0.4f, 0.03f, 1), S(2800, 1200, 0.35f, 0.05f, 1), S(3900, 3200, 0.3f, 0.04f, 0), S(3000, 2400, 0.25f, 0.06f, 0) };
static const Synth::Step kIceB[] = { S(150, 70, 0.55f, 0.16f, 2), S(4400, 2000, 0.3f, 0.12f, 1) };
static const Synth::Step kHeavyA[] = { S(1500, 300, 0.7f, 0.08f, 1), S(80, 35, 0.7f, 0.5f, 1) };
static const Synth::Step kHeavyB[] = { S(90, 30, 0.9f, 0.45f, 0), S(55, 28, 0.7f, 0.4f, 2) };
static const Synth::Step kScrapeA[] = { S(2600, 2000, 0.35f, 0.12f, 1), S(2200, 1500, 0.30f, 0.14f, 1), S(1800, 1400, 0.25f, 0.12f, 1) };
static const Synth::Step kScrapeB[] = { S(1100, 800, 0.15f, 0.34f, 3) };
static const Synth::Step kNitro[] = { S(300, 3000, 0.45f, 0.5f, 1), S(3000, 900, 0.3f, 0.4f, 1) };
static const Synth::Step kShift[] = { S(1900, 500, 0.22f, 0.025f, 1), S(190, 95, 0.40f, 0.07f, 2) };
static const Synth::Step kLap[] = { S(784, 784, 0.35f, 0.08f, 2), S(988, 988, 0.35f, 0.08f, 2), S(1319, 1319, 0.4f, 0.22f, 2) };
static const Synth::Step kFinish[] = {
    S(523, 523, 0.4f, 0.12f, 0), S(659, 659, 0.4f, 0.12f, 0), S(784, 784, 0.4f, 0.12f, 0),
    S(1047, 1047, 0.45f, 0.3f, 0), S(784, 784, 0.4f, 0.12f, 0), S(1047, 1047, 0.5f, 0.6f, 0) };
#undef S

template <typename T, int N> static int countOf(const T (&)[N]) { return N; }

static const int16_t kAdpcmStep[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t kAdpcmIdx[8] = {-1,-1,-1,-1,2,4,6,8};

void Synth::playVoice(const uint8_t* data, int bytes) {
    if (!data || bytes < 8 || bytes > VOICE_MAX) return;
    if (m_vSeen != m_vSerial) return;                         // the previous request has not been picked up yet
    int w = (m_vBuf == 0) ? 1 : 0;                            // write into the buffer the audio thread is not reading
    if (!m_vbuf[w]) { m_vbuf[w] = (uint8_t*)plat::allocLarge(VOICE_MAX); if (!m_vbuf[w]) return; }
    memcpy(m_vbuf[w], data, bytes);
    m_vReqBuf = w; m_vReqLen = bytes;
    m_vSerial = m_vSerial + 1;
}

// next decoded sample (-32768..32767) of the running clip
int Synth::voiceNext() {
    if (m_vPos >= m_vLen) { m_vBuf = -1; return 0; }
    const uint8_t b = m_vbuf[m_vBuf][m_vPos];
    int code = m_vNib ? (b >> 4) : (b & 15);
    if (m_vNib) { m_vNib = 0; ++m_vPos; } else m_vNib = 1;
    int step = kAdpcmStep[m_vIdx], d = step >> 3;
    if (code & 4) d += step;
    if (code & 2) d += step >> 1;
    if (code & 1) d += step >> 2;
    m_vPred += (code & 8) ? -d : d;
    if (m_vPred > 32767) m_vPred = 32767; else if (m_vPred < -32768) m_vPred = -32768;
    m_vIdx += kAdpcmIdx[code & 7];
    if (m_vIdx < 0) m_vIdx = 0; else if (m_vIdx > 88) m_vIdx = 88;
    return m_vPred;
}

void Synth::play(int sfx) {
    unsigned w = m_reqW;
    m_req[w & 7] = sfx;
    m_reqW = w + 1;
}

void Synth::start(int sfx) {
    const void* prog = nullptr; int n = 0; int slot = 0;
    const void* prog2 = nullptr; int n2 = 0;                 // optional layered program (slot 2)
    switch (sfx) {
    case plat::SFX_BEEP_LOW:  prog = kBeepLow;  n = countOf(kBeepLow);  slot = 0; break;
    case plat::SFX_BEEP_HIGH: prog = kBeepHigh; n = countOf(kBeepHigh); slot = 0; break;
    case plat::SFX_MENU_MOVE: prog = kMove;     n = countOf(kMove);     slot = 0; break;
    case plat::SFX_MENU_OK:   prog = kOk;       n = countOf(kOk);       slot = 0; break;
    case plat::SFX_CRASH:     prog = kCrash;    n = countOf(kCrash);    slot = 1; break;
    case plat::SFX_BUMP:      prog = kBump;     n = countOf(kBump);     slot = 1; prog2 = kBumpB;   n2 = countOf(kBumpB);   break;
    case plat::SFX_NITRO:     prog = kNitro;    n = countOf(kNitro);    slot = 2; break;
    case plat::SFX_LAP:       prog = kLap;      n = countOf(kLap);      slot = 0; break;
    case plat::SFX_FINISH:    prog = kFinish;   n = countOf(kFinish);   slot = 0; break;
    case plat::SFX_HIT_SOFT:  prog = kSoftA;    n = countOf(kSoftA);    slot = 1; prog2 = kSoftB;   n2 = countOf(kSoftB);   break;
    case plat::SFX_HIT_WOOD:  prog = kWoodA;    n = countOf(kWoodA);    slot = 1; prog2 = kWoodB;   n2 = countOf(kWoodB);   break;
    case plat::SFX_HIT_ROCK:  prog = kRockA;    n = countOf(kRockA);    slot = 1; prog2 = kRockB;   n2 = countOf(kRockB);   break;
    case plat::SFX_HIT_METAL: prog = kMetalA;   n = countOf(kMetalA);   slot = 1; prog2 = kMetalB;  n2 = countOf(kMetalB);  break;
    case plat::SFX_HIT_ICE:   prog = kIceA;     n = countOf(kIceA);     slot = 1; prog2 = kIceB;    n2 = countOf(kIceB);    break;
    case plat::SFX_HIT_HEAVY: prog = kHeavyA;   n = countOf(kHeavyA);   slot = 1; prog2 = kHeavyB;  n2 = countOf(kHeavyB);  break;
    case plat::SFX_SCRAPE:    prog = kScrapeA;  n = countOf(kScrapeA);  slot = 1; prog2 = kScrapeB; n2 = countOf(kScrapeB); break;
    case plat::SFX_SHIFT:     prog = kShift;    n = countOf(kShift);    slot = 2; break;
    default: return;
    }
    Voice& v = m_voice[slot];
    v.steps = static_cast<const Step*>(prog);
    v.count = n; v.cur = 0; v.t = 0; v.phase = 0;
    if (prog2) {
        Voice& w = m_voice[2];
        w.steps = static_cast<const Step*>(prog2);
        w.count = n2; w.cur = 0; w.t = 0; w.phase = 0;
    }
}

// ------------------------------------------------------------------ music
static float midiHz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

void Synth::musicStep(int rate) {
    const Sequence& sq = g_songs[m_song];
    const float dt = 1.0f / rate;
    int st = m_step;
    auto trigger = [&](MVoice& v, int note, float peak, float decaySec, float sustain) {
        v.inc = midiHz(note) * dt;
        v.amp = peak;
        v.sus = peak * sustain;
        v.dec = expf(-1.0f / (decaySec * rate));
        v.gate = true;
    };
    uint8_t l = sq.lead[st];
    if (l == NOTE_OFF) m_lead.gate = false;
    else if (l != NOTE_HOLD) { trigger(m_lead, l, 1.0f, 0.25f, 0.6f); m_noteT = 0; }
    uint8_t b = sq.bass[st];
    if (b == NOTE_OFF) m_bass.gate = false;
    else if (b != NOTE_HOLD) trigger(m_bass, b, 1.0f, 0.12f, 0.35f);
    int a = sq.chord[st][st % 3] + ((m_pass & 1) ? 12 : 0);
    trigger(m_arp, a, 1.0f, 0.05f, 0.0f);
    uint8_t d = sq.drums[st];
    if (d & DRUM_KICK) { m_kickEnv = 1.0f; m_kickPhase = 0; }
    if (d & DRUM_SNARE) m_snEnv = 1.0f;
    if (d & DRUM_HAT) m_hatEnv = (st & 1) ? 0.6f : 1.0f;
}

float Synth::musicSample(float dt) {
    m_musVolS += (m_musVol - m_musVolS) * 0.0005f;
    if (m_song < 0 || m_musVolS < 0.001f) return 0;
    const Sequence& sq = g_songs[m_song];
    const float stepDur = 60.0f / sq.bpm / 4.0f;
    m_stepT += dt;
    if (m_stepT >= stepDur) {
        m_stepT -= stepDur;
        if (++m_step >= sq.steps) { m_step = 0; ++m_pass; }
        musicStep((int)(1.0f / dt + 0.5f));
    }
    m_noteT += dt;
    float s = 0;
    // lead: 25% pulse with delayed vibrato, softened by a low-pass
    {
        MVoice& v = m_lead;
        v.amp = v.gate ? v.sus + (v.amp - v.sus) * v.dec : v.amp * 0.9992f;
        m_lfo += 5.5f * dt; if (m_lfo >= 1) m_lfo -= 1;
        float vib = m_noteT > 0.15f ? (4.0f * fabsf(m_lfo - 0.5f) - 1.0f) * 0.005f : 0.0f;
        v.phase += v.inc * (1.0f + vib); if (v.phase >= 1) v.phase -= 1;
        float w = v.phase < 0.25f ? 1.0f : -0.45f;
        v.lp += (w - v.lp) * 0.35f;
        s += v.lp * v.amp * 0.20f;
    }
    // bass: low-passed saw
    {
        MVoice& v = m_bass;
        v.amp = v.gate ? v.sus + (v.amp - v.sus) * v.dec : v.amp * 0.999f;
        v.phase += v.inc; if (v.phase >= 1) v.phase -= 1;
        v.lp += ((v.phase * 2 - 1) - v.lp) * 0.12f;
        s += v.lp * v.amp * 0.42f;
    }
    // arpeggio: soft triangle blips
    {
        MVoice& v = m_arp;
        v.amp *= v.dec;
        v.phase += v.inc; if (v.phase >= 1) v.phase -= 1;
        float tri = v.phase < 0.5f ? v.phase * 4 - 1 : 3 - v.phase * 4;
        s += tri * v.amp * 0.10f;
    }
    // drums
    if (m_kickEnv > 0.001f) {
        m_kickPhase += (42.0f + 120.0f * m_kickEnv * m_kickEnv) * dt;
        if (m_kickPhase >= 1) m_kickPhase -= 1;
        float sn = m_kickPhase < 0.5f ? m_kickPhase * 4 - 1 : 3 - m_kickPhase * 4;
        s += sn * m_kickEnv * 0.55f;
        m_kickEnv *= 0.99955f;
    }
    if (m_snEnv > 0.001f) {
        m_snPhase += 190.0f * dt; if (m_snPhase >= 1) m_snPhase -= 1;
        s += (noise() * 0.8f + (m_snPhase < 0.5f ? 0.3f : -0.3f)) * m_snEnv * 0.26f;
        m_snEnv *= 0.99875f;
    }
    if (m_hatEnv > 0.001f) {
        float nz = noise();
        m_hatLp += (nz - m_hatLp) * 0.5f;
        s += (nz - m_hatLp) * m_hatEnv * 0.16f;
        m_hatEnv *= 0.9965f;
    }
    return s * m_musVolS * 0.85f;
}

void Synth::render(int16_t* out, int n, int rate) {
    while (m_reqR != m_reqW) { start(m_req[m_reqR & 7]); ++m_reqR; }
    if (m_songSeen != m_songSerial) {          // song change request from the game
        m_songSeen = m_songSerial;
        int want = m_songReq;
        if (want >= SONG_COUNT) want = -1;
        if (want != m_song || m_songRestart) {
            m_song = want;
            m_step = 0; m_pass = 0; m_stepT = 0;
            m_lead.gate = m_bass.gate = false;
            m_lead.amp = m_bass.amp = m_arp.amp = 0;
            if (m_song >= 0) musicStep(rate);
        }
    }
    if (m_vSeen != m_vSerial) {                // new voice clip
        m_vSeen = m_vSerial;
        m_vBuf = m_vReqBuf; m_vLen = m_vReqLen; m_vPos = 4; m_vNib = 0;
        const uint8_t* h = m_vbuf[m_vBuf];
        m_vPred = (int16_t)(h[0] | (h[1] << 8)); m_vIdx = h[2] > 88 ? 88 : h[2];
        m_vPrev = m_vPred; m_vNext = voiceNext(); m_vFrac = 0;
    }
    const float vStep = 8000.0f / rate;
    const float dt = 1.0f / rate;
    const float rpmT = m_rpm, volT = m_vol, thr = m_thr;
    const float rainT = m_rain, rumT = m_rumble, skidT = m_skid, master = m_master;
    for (int i = 0; i < n; ++i) {
        // smooth control values to avoid zipper noise
        m_rpmS += (rpmT - m_rpmS) * 0.002f;
        m_volS += (volT - m_volS) * 0.002f;
        m_rumbleS += (rumT - m_rumbleS) * 0.002f;
        m_skidS += (skidT - m_skidS) * 0.002f;
        m_rainS += (rainT - m_rainS) * 0.0005f;

        float s = 0;
        // engine: saw + sub square, combustion noise, low-passed
        if (m_volS > 0.001f) {
            float f = 34.0f + m_rpmS * 165.0f;
            m_ePhase += f * dt; if (m_ePhase >= 1) m_ePhase -= 1;
            m_ePhase2 += f * 0.5f * dt; if (m_ePhase2 >= 1) m_ePhase2 -= 1;
            float saw = m_ePhase * 2 - 1;
            float sq = m_ePhase2 < 0.35f ? 1.0f : -1.0f;
            float raw = saw * 0.6f + sq * 0.4f + noise() * 0.12f * (0.5f + thr);
            float cut = 0.05f + 0.12f * m_rpmS + 0.05f * thr;
            m_eLp += (raw - m_eLp) * cut;
            s += m_eLp * m_volS * 0.9f;
        }
        // rumble strip / gravel
        if (m_rumbleS > 0.001f) {
            m_amPhase += 28.0f * dt; if (m_amPhase >= 1) m_amPhase -= 1;
            m_nLp += (noise() - m_nLp) * 0.04f;
            s += m_nLp * m_rumbleS * (m_amPhase < 0.5f ? 1.0f : 0.35f) * 1.6f;
        }
        // tyre squeal: band-passed noise
        if (m_rainS > 0.001f) {                               // rain: high-passed hiss
            float nz = noise();
            m_rLp += (nz - m_rLp) * 0.30f;
            s += (nz - m_rLp) * m_rainS * 0.07f;
        }
        if (m_skidS > 0.001f) {
            float nz = noise();
            m_nHp += (nz - m_nHp) * 0.3f;
            float bp = m_nHp - m_nLp * 0.5f;
            s += bp * m_skidS * 0.25f;
        }
        s += musicSample(dt);
        // effects
        for (Voice& v : m_voice) {
            if (!v.steps) continue;
            const Step& st = v.steps[v.cur];
            float u = v.t / st.dur;
            float f = st.f0 + (st.f1 - st.f0) * u;
            float env = st.vol * (u < 0.05f ? u / 0.05f : (1.0f - u * 0.6f));
            float w;
            v.phase += f * dt; if (v.phase >= 1) v.phase -= 1;
            switch (st.wave) {
            case 0: w = v.phase < 0.5f ? 0.6f : -0.6f; break;
            case 1: w = noise() * (0.5f + 0.5f * (v.phase < 0.5f ? 1.0f : 0.0f)); break;
            case 2: w = (v.phase < 0.5f ? v.phase * 4 - 1 : 3 - v.phase * 4) * 0.9f; break;
            default: w = v.phase * 2 - 1; break;
            }
            s += w * env;
            v.t += dt;
            if (v.t >= st.dur) { v.t = 0; if (++v.cur >= v.count) v.steps = nullptr; }
        }
        if (m_vBuf >= 0) {                                    // coach voice, linear resample 8 kHz -> output rate
            float v = (m_vPrev + (m_vNext - m_vPrev) * m_vFrac) * (1.0f / 32768.0f);
            s += v * 0.9f;
            m_vFrac += vStep;
            while (m_vFrac >= 1.0f && m_vBuf >= 0) { m_vFrac -= 1.0f; m_vPrev = m_vNext; m_vNext = voiceNext(); }
        }
        s *= master;
        if (s > 1) s = 1; else if (s < -1) s = -1;
        out[i] = (int16_t)(s * 26000);
    }
}
