// music.h - one theme per circuit, written as text and compiled at boot into
// step sequences played by the Synth (16 steps per bar).
#pragma once
#include <stdint.h>

constexpr int MUSIC_MAX_STEPS = 256;

enum : uint8_t { NOTE_OFF = 0, NOTE_HOLD = 1 };   // other values: MIDI note numbers

struct Sequence {
    const char* title = "";
    int bpm = 120;
    int steps = 0;
    uint8_t lead[MUSIC_MAX_STEPS];
    uint8_t bass[MUSIC_MAX_STEPS];
    uint8_t chord[MUSIC_MAX_STEPS][3];
    uint8_t drums[MUSIC_MAX_STEPS];   // DRUM_* bits
};
enum : uint8_t { DRUM_KICK = 1, DRUM_SNARE = 2, DRUM_HAT = 4 };

constexpr int SONG_COUNT = 6;
extern Sequence g_songs[SONG_COUNT];

void music_init();   // parses the song texts (call once at boot)
