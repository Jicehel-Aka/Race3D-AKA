// online.h - client side of the online races: settings file, connection to the race server (binary protocol),
// lobby information and the replicated cars. The transport (Wi-Fi / TLS / WebSocket) lives behind plat::net*.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "platform.h"

class Race;

namespace online {

struct Settings {
    plat::NetConfig net;
    char room[12] = "AKA1";
    char name[12] = "AKA";
};

enum LoadResult { LOAD_OK = 0, LOAD_NO_FILE, LOAD_NO_WIFI };
enum Phase { PH_LOBBY = 0, PH_COUNTDOWN, PH_RACING, PH_RESULTS };

struct Info {
    bool haveWelcome = false, haveSnap = false;
    int slot = 7;            // our car
    bool host = false;
    int track = 0, laps = 3, diff = 1;
    int phase = PH_LOBBY;
    float raceTime = 0;
    uint8_t humanMask = 0;   // bit i set: a real player drives car i
    uint8_t retiredMask = 0;
    uint32_t snapSeq = 0;    // grows with every snapshot received
};

// Reads /sdcard/race3d/online.txt (key=value lines). Missing keys keep their defaults (pre-filled public server).
int  loadSettings(Settings& s);
const char* settingsPath();

void begin(const Settings& s);      // start Wi-Fi + connection (background)
void end();                         // disconnect and free the radio memory
int  status();                      // plat::NetStatus
const char* error();
bool isOnline();                    // connected to the server
void poll();                        // call every frame: consumes the received messages
const Info& info();

bool kicked();                      // the server disqualified us (anti-cheat)
bool takeLeft(char* out, size_t cap);   // true once when another player left during the race (name in out)
void applyNames(Race& r);           // players' names on the cars

void sendConfigure(int track, int laps, int diff);   // host only
void sendStart();
void sendLobby();
void sendTelemetry(const Race& r); // our car, rate limited to 20 Hz by the caller
// copy the latest received state of the other cars into the race (call after poll)
void feed(Race& r);

} // namespace online
