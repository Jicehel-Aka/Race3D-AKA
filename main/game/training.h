// training.h - the training circuit and its scripted lessons (text + coach voice).
#pragma once
#include "race.h"

namespace training {
constexpr int LESSONS = 8;
const char* text(int lesson);                                   // English key, translated by TR()
void reset();
// Called every physics step of a training race. say(text key, voice number) shows the subtitle and plays the clip.
void update(Race& r, void (*say)(const char* key, int voice));
void finished(void (*say)(const char* key, int voice));         // last lesson: the player crossed the line
}
