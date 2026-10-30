// synth.h - tiny software synthesizer: engine, tyres, rumble and sound effects.
// render() is called from the audio thread, the setters from the game thread.
#pragma once
#include <stdint.h>

class Synth {
public:
    struct Step { float f0, f1, vol, dur; uint8_t wave; };   // wave: 0 square 1 noise 2 triangle 3 saw
    void setEngine(float rpm01, float throttle, float volume) { m_rpm = rpm01; m_thr = throttle; m_vol = volume; }
    void setNoise(float rumble, float skid) { m_rumble = rumble; m_skid = skid; }
    void play(int sfx);                          // plat::Sfx id
    void render(int16_t* out, int n, int rate);  // mono 16-bit
    void setMaster(float v) { m_master = v; }
    // background music: song index from music.h, -1 = silence
    void setSong(int song, bool restart = false) { m_songReq = song; m_songRestart = restart; m_songSerial = m_songSerial + 1; }
    void setMusicVolume(float v) { m_musVol = v; }

private:
    struct Voice { const Step* steps = nullptr; int count = 0, cur = 0; float t = 0, phase = 0; };

    volatile float m_rpm = 0, m_thr = 0, m_vol = 0, m_rumble = 0, m_skid = 0, m_master = 0.8f;
    volatile int m_req[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    volatile unsigned m_reqW = 0;
    unsigned m_reqR = 0;

    Voice m_voice[3];
    float m_ePhase = 0, m_ePhase2 = 0, m_eLp = 0, m_rpmS = 0, m_volS = 0;
    float m_nLp = 0, m_nHp = 0, m_rumbleS = 0, m_skidS = 0, m_amPhase = 0;
    uint32_t m_lfsr = 0xACE1u;

    // music
    struct MVoice { float phase = 0, inc = 0, amp = 0, dec = 1, sus = 0, lp = 0; bool gate = false; };
    volatile int m_songReq = -1;
    volatile bool m_songRestart = false;
    volatile unsigned m_songSerial = 0;
    volatile float m_musVol = 0.8f;
    unsigned m_songSeen = 0;
    int m_song = -1, m_step = 0, m_pass = 0;
    float m_stepT = 0, m_musVolS = 0, m_lfo = 0, m_noteT = 0;
    MVoice m_lead, m_bass, m_arp;
    float m_kickEnv = 0, m_kickPhase = 0, m_snEnv = 0, m_hatEnv = 0, m_hatLp = 0, m_snPhase = 0;
    void musicStep(int rate);
    float musicSample(float dt);

    float noise() { m_lfsr ^= m_lfsr << 13; m_lfsr ^= m_lfsr >> 17; m_lfsr ^= m_lfsr << 5; return (int32_t)m_lfsr * (1.0f / 2147483648.0f); }
    void start(int sfx);
};
