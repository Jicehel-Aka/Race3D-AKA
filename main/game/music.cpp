// music.cpp - the circuit themes (one song per look).
// Lead: one token per 16th note. "-" holds the previous note, "." is silence.
// Chords: one per bar; the bass and the arpeggio are generated from them.
// Drums: k kick, s snare, h hat, x kick+hat, o snare+hat, . nothing.
#include "music.h"
#include "platform.h"
#include <new>
#include <string.h>
#include <stdio.h>

Sequence* g_songs = nullptr;      // allocated in PSRAM by music_init (9 KB less internal RAM)

namespace {

struct SongText {
    const char* title;
    int bpm;
    int bassStyle;       // 0 octave eighths, 1 root-fifth eighths, 2 sixteenth pulse,
                         // 3 syncopated, 4 slow pad, 5 driving eighths
    int leadTranspose;
    const char* chords;
    const char* lead;
    const char* drums;
};

const SongText kSongs[SONG_COUNT] = {
    // Sunset Coast - synthwave in A minor
    { "SUNSET DRIVE", 112, 0, 0,
      "Am F C G Am F C E",
      "E5 - - - D5 - C5 - A4 - - - - - - . |"
      "C5 - - - D5 - E5 - F5 - - - E5 - D5 - |"
      "E5 - - - G5 - - - E5 - D5 - C5 - - . |"
      "D5 - - - - - - - B4 - C5 - D5 - - . |"
      "E5 - - - A5 - - - G5 - E5 - D5 - C5 - |"
      "D5 - - - C5 - A4 - C5 - - - F5 - - . |"
      "E5 - - - D5 - C5 - G4 - - - C5 - E5 - |"
      "D5 - - - - - C5 - B4 - - - G#4 - - .",
      "k.h.s.h.k.h.s.hh" },
    // Alpine Pass - bright and fast in D major
    { "SUMMIT RUSH", 138, 1, -12,
      "D A Bm G D A G A",
      "F#5 - A5 - D6 - A5 - F#5 - E5 - D5 - E5 - |"
      "E5 - - - C#5 - E5 - A5 - - - G5 - F#5 - |"
      "F#5 - - - D5 - F#5 - B5 - - - A5 - F#5 - |"
      "G5 - - - - - B5 - A5 - G5 - F#5 - E5 - |"
      "D5 - F#5 - A5 - - - D6 - - - C#6 - B5 - |"
      "A5 - - - E5 - - - C#5 - E5 - A5 - - . |"
      "B5 - - - A5 - G5 - F#5 - G5 - A5 - B5 - |"
      "A5 - - - - - - - E5 - - - A5 - - .",
      "k.h.s.hkk.h.s.h." },
    // Neon City - dark, driving, E minor
    { "NEON NIGHTS", 124, 2, -12,
      "Em C D B Em C Am B",
      "E5 . E5 . G5 . . B5 - - . A5 . G5 . . |"
      "E5 - - - . . D5 . E5 . G5 - - - . . |"
      "F#5 . F#5 . A5 . . D6 - - . C6 . A5 . . |"
      "B5 - - - - - . . A5 . G5 . F#5 - D#5 - |"
      "E5 . G5 . B5 . E6 - - - . D6 . B5 . . |"
      "C6 - - - B5 . G5 . E5 - - - G5 . . . |"
      "A5 . C6 . E6 - - - D6 . C6 . B5 . A5 . |"
      "B5 - - - - - - - D#5 - - - F#5 - - .",
      "k.h.o.hkk.h.o.hh" },
    // Palm Bay - sunny, bouncy, syncopated bass, G major
    { "SEA BREEZE", 128, 3, 0,
      "G Em C D G Em Am D",
      "B5 - D6 - - B5 - G5 - - A5 - B5 - - . |"
      "G5 - - E5 - G5 - - B5 - A5 - G5 - E5 - |"
      "E5 - G5 - - E5 - C5 - - D5 - E5 - - . |"
      "F#5 - A5 - - F#5 - D5 - E5 - F#5 - A5 - - |"
      "D6 - - B5 - D6 - - E6 - D6 - B5 - G5 - |"
      "B5 - - G5 - B5 - - A5 - G5 - E5 - - . |"
      "C6 - - A5 - C6 - E6 - - D6 - C6 - A5 - |"
      "A5 - - F#5 - A5 - D6 - - C#6 - A5 - - .",
      "x..hk.o.x..hk.o." },
    // Glacier Run - wide open and shimmering, D minor, slow bass pads
    { "POLAR LIGHTS", 96, 4, -12,
      "Dm Bb F C Dm Bb Gm A",
      "A5 - - - - - - - F5 - - - - - D5 - |"
      "D6 - - - - - - - C6 - - - A5 - - - |"
      "C6 - - - - - A5 - F5 - - - A5 - C6 - |"
      "E6 - - - - - - - D6 - - - C6 - - . |"
      "A5 - - - D6 - - - F6 - - - E6 - D6 - |"
      "D6 - - - - - C6 - Bb5 - - - A5 - - - |"
      "G5 - - - Bb5 - - - D6 - - - G6 - - - |"
      "E6 - - - - - - - C#6 - - - A5 - - .",
      "k...h.h.s...h.hk" },
    // Night Harbor - dark industrial pulse, F# minor
    { "DOCK RUNNER", 118, 5, -12,
      "F#m D A E F#m D Bm C#",
      "F#5 . . F#5 . . A5 . F#5 . . . E5 . . . |"
      "D5 . . D5 . . F#5 . D5 . . . C#5 . . . |"
      "E5 . . E5 . . A5 . E5 . . . C#5 . . . |"
      "B4 . . B4 . . E5 . B4 . . . G#4 . . . |"
      "F#5 . A5 . C#6 . . . A5 . F#5 . E5 - - . |"
      "D5 . F#5 . A5 . . . F#5 . D5 . C#5 - - . |"
      "D6 - - - C#6 . B5 . . . A5 . F#5 - - . |"
      "E5 . G#5 . C#6 - - - B5 . G#5 . F5 - - .",
      "x.h.o.h.x.h.o.hk" },
};


int noteNumber(const char* t) {   // "C#5" -> 73
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    if (*t < 'A' || *t > 'G') return -1;
    int n = base[*t - 'A'];
    ++t;
    if (*t == '#') { ++n; ++t; } else if (*t == 'b') { --n; ++t; }
    if (*t < '0' || *t > '9') return -1;
    return n + 12 * (*t - '0' + 1);
}

// "C#m" -> root pitch class + minor flag
bool parseChord(const char* t, int& root, bool& minor) {
    char buf[4] = { t[0], '4', 0, 0 };
    const char* p = t + 1;
    if (*p == '#' || *p == 'b') { buf[1] = *p; buf[2] = '4'; ++p; }
    int n = noteNumber(buf);
    if (n < 0) return false;
    root = n % 12;
    minor = (*p == 'm');
    return true;
}

const char* nextToken(const char* p, char* out, int cap) {
    while (*p == ' ' || *p == '|') ++p;
    int k = 0;
    while (*p && *p != ' ' && *p != '|' && k < cap - 1) out[k++] = *p++;
    out[k] = 0;
    return p;
}

void compile(const SongText& src, Sequence& seq) {
    seq.title = src.title;
    seq.bpm = src.bpm;
    // chords, one per bar
    int roots[16]; bool minors[16]; int bars = 0;
    char tok[8];
    for (const char* p = src.chords; *p && bars < 16;) {
        p = nextToken(p, tok, sizeof tok);
        if (!tok[0]) break;
        if (parseChord(tok, roots[bars], minors[bars])) ++bars;
    }
    seq.steps = bars * 16;
    if (seq.steps > MUSIC_MAX_STEPS) seq.steps = MUSIC_MAX_STEPS;
    // lead
    int i = 0;
    for (const char* p = src.lead; *p && i < seq.steps;) {
        p = nextToken(p, tok, sizeof tok);
        if (!tok[0]) break;
        if (tok[0] == '-') seq.lead[i++] = NOTE_HOLD;
        else if (tok[0] == '.') seq.lead[i++] = NOTE_OFF;
        else { int n = noteNumber(tok); seq.lead[i++] = n > 0 ? (uint8_t)(n + src.leadTranspose) : (uint8_t)NOTE_OFF; }
    }
#ifdef HOST_BUILD
    if (i != seq.steps) printf("music: '%s' lead has %d steps, expected %d\n", src.title, i, seq.steps);
#endif
    for (; i < seq.steps; ++i) seq.lead[i] = NOTE_OFF;
    // bass + chord tones
    for (int s = 0; s < seq.steps; ++s) {
        int bar = s / 16, st = s % 16;
        int r = roots[bar], third = minors[bar] ? 3 : 4;
        int bassRoot = 36 + r;                      // C2..B2
        if (bassRoot > 43) bassRoot -= 12;
        uint8_t b = NOTE_HOLD;
        switch (src.bassStyle) {
        case 0: if ((st & 1) == 0) b = (uint8_t)(bassRoot + ((st & 2) ? 12 : 0)); break;
        case 1: {
            static const int pat[8] = { 0, 0, 7, 0, 12, 0, 7, 0 };
            if ((st & 1) == 0) b = (uint8_t)(bassRoot + pat[st / 2]);
            break;
        }
        case 3: {                                   // syncopated, island style
            if (st == 0 || st == 3 || st == 6 || st == 8 || st == 11 || st == 14)
                b = (uint8_t)(bassRoot + (st == 3 ? 12 : (st == 6 || st == 14) ? 7 : 0));
            break;
        }
        case 4:                                     // slow pad: root, then the fifth
            if (st == 0) b = (uint8_t)bassRoot;
            else if (st == 8) b = (uint8_t)(bassRoot + 7);
            break;
        case 5:                                     // driving eighths with a fifth on the turnaround
            if ((st & 1) == 0) b = (uint8_t)(bassRoot + (((st & 7) == 6) ? 7 : 0));
            break;
        default: b = (uint8_t)(bassRoot + ((st == 14) ? 12 : 0)); break;
        }
        seq.bass[s] = b;
        seq.chord[s][0] = (uint8_t)(60 + r);
        seq.chord[s][1] = (uint8_t)(60 + r + third);
        seq.chord[s][2] = (uint8_t)(60 + r + 7);
    }
    // drums (pattern repeats)
    int dl = 0;
    uint8_t pat[64];
    for (const char* p = src.drums; *p && dl < 64; ++p) {
        uint8_t v;
        switch (*p) {
        case 'k': v = DRUM_KICK; break;
        case 's': v = DRUM_SNARE; break;
        case 'h': v = DRUM_HAT; break;
        case 'x': v = DRUM_KICK | DRUM_HAT; break;
        case 'o': v = DRUM_SNARE | DRUM_HAT; break;
        case '.': v = 0; break;
        default: continue;
        }
        pat[dl++] = v;
    }
    for (int s = 0; s < seq.steps; ++s) seq.drums[s] = dl ? pat[s % dl] : 0;
}

} // namespace

void music_init() {
    if (!g_songs) {
        g_songs = (Sequence*)plat::allocLarge(sizeof(Sequence) * SONG_COUNT);
        for (int k = 0; k < SONG_COUNT; ++k) new (&g_songs[k]) Sequence();
    }
    static bool done = false;
    if (done) return;
    done = true;
    for (int k = 0; k < SONG_COUNT; ++k) compile(kSongs[k], g_songs[k]);
}
