// online.cpp - see online.h
#include "online.h"
#include "race.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

namespace online {
namespace {

Info g_info;
struct RCar { float dist, x, speed, steer; uint8_t flags, place; };
RCar g_cars[CAR_COUNT];
uint32_t g_fedSeq = 0;
uint16_t g_seq = 0;
char g_names[CAR_COUNT][12];            // names of the players, from the names packet (0xA7)
const char* g_def[CAR_COUNT];           // default (AI) names of the race cars
uint8_t g_prevHuman = 0, g_leftPending = 0;
char g_leftName[12];
bool g_kicked = false;
const int kLapsTab[4] = { 1, 2, 3, 5 };

float rdf(const uint8_t* p) { float f; memcpy(&f, p, 4); return f; }
int16_t rd16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
void wrf(uint8_t* p, float f) { memcpy(p, &f, 4); }

void trim(char* s) {
    size_t n = strlen(s);
    while (n && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
    char* a = s;
    while (*a && (unsigned char)*a <= ' ') ++a;
    if (a != s) memmove(s, a, strlen(a) + 1);
}

void clean(char* s, size_t cap, bool upper) {       // keep it URL-safe: letters, digits, - and _
    size_t n = 0;
    for (size_t i = 0; s[i] && n < cap - 1; ++i) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_') s[n++] = upper ? (char)toupper((unsigned char)c) : c;
        else if (c == ' ') s[n++] = '_';
    }
    s[n] = 0;
}

void parseHost(Settings& st, char* v) {              // accepts host, host:port, wss://host/path, ws://host:port/path
    if (!strncmp(v, "wss://", 6)) { v += 6; st.net.tls = true; st.net.port = 443; }
    else if (!strncmp(v, "ws://", 5)) { v += 5; st.net.tls = false; st.net.port = 80; }
    else if (!strncmp(v, "https://", 8)) { v += 8; st.net.tls = true; st.net.port = 443; }
    else if (!strncmp(v, "http://", 7)) { v += 7; st.net.tls = false; st.net.port = 80; }
    char* slash = strchr(v, '/');
    if (slash) *slash = 0;
    char* colon = strchr(v, ':');
    if (colon) { *colon = 0; st.net.port = (uint16_t)atoi(colon + 1); }
    snprintf(st.net.host, sizeof st.net.host, "%s", v);
}

void join_query(Settings& st) {
    char id[20];
    uint32_t r = plat::millis() * 2654435761u + 0x9E3779B9u;
    snprintf(id, sizeof id, "aka-%08x", (unsigned)r);
    snprintf(st.net.path, sizeof st.net.path, "/ws/race?mode=binary&room=%s&platform=AKA&name=%s&clientId=%s", st.room, st.name, id);
}

bool send_json(const char* s) { return plat::netSend(false, s, strlen(s)); }

} // namespace

const char* settingsPath() { return "/sdcard/race3d/online.txt"; }

int loadSettings(Settings& st) {
    st = Settings();
    snprintf(st.net.host, sizeof st.net.host, "aka-turbo-race-server.onrender.com");
    st.net.port = 443; st.net.tls = true;
    char buf[1024];
#ifdef HOST_BUILD
    if (!plat::readTextFile(settingsPath(), buf, sizeof buf)) buf[0] = 0;      // PC: no Wi-Fi to set up, the public server is the default
#else
    if (!plat::readTextFile(settingsPath(), buf, sizeof buf)) return LOAD_NO_FILE;
#endif
    for (char* line = buf; *line;) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* next = nl ? nl + 1 : line + strlen(line);
        trim(line);
        char* eq = (*line && *line != '#' && *line != ';') ? strchr(line, '=') : nullptr;
        if (eq) {
            *eq = 0;
            char* k = line; char* v = eq + 1;
            trim(k); trim(v);
            for (char* p = k; *p; ++p) *p = (char)tolower((unsigned char)*p);
            if (!strcmp(k, "ssid") || !strcmp(k, "wifi")) snprintf(st.net.ssid, sizeof st.net.ssid, "%s", v);
            else if (!strcmp(k, "pass") || !strcmp(k, "password")) snprintf(st.net.pass, sizeof st.net.pass, "%s", v);
            else if (!strcmp(k, "room")) snprintf(st.room, sizeof st.room, "%s", v);
            else if (!strcmp(k, "name")) snprintf(st.name, sizeof st.name, "%s", v);
            else if (!strcmp(k, "host") || !strcmp(k, "server")) { if (*v) parseHost(st, v); }
            else if (!strcmp(k, "port")) { int p = atoi(v); if (p > 0 && p < 65536) st.net.port = (uint16_t)p; }
            else if (!strcmp(k, "tls")) st.net.tls = !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
        }
        line = next;
    }
    clean(st.room, sizeof st.room, true);
    clean(st.name, sizeof st.name, true);
    if (!st.room[0]) snprintf(st.room, sizeof st.room, "AKA1");
    if (!st.name[0]) snprintf(st.name, sizeof st.name, "AKA");
#ifndef HOST_BUILD
    if (!st.net.ssid[0]) return LOAD_NO_WIFI;
#endif
    return LOAD_OK;
}

void begin(const Settings& s) {
    Settings st = s;
    join_query(st);
    g_info = Info();
    memset(g_cars, 0, sizeof g_cars);
    g_fedSeq = 0; g_seq = 0;
    memset(g_names, 0, sizeof g_names); memset(g_def, 0, sizeof g_def);
    g_prevHuman = 0; g_leftPending = 0; g_leftName[0] = 0; g_kicked = false;
    plat::netStart(st.net);
}

void end() { plat::netStop(); g_info = Info(); }
int status() { return plat::netStatus(); }
const char* error() { return plat::netError(); }
bool isOnline() { return plat::netStatus() == plat::NET_ONLINE; }
const Info& info() { return g_info; }

void poll() {
    uint8_t buf[300];
    for (int guard = 0; guard < 12; ++guard) {
        bool bin = false;
        int n = plat::netRecv(buf, sizeof buf, &bin);
        if (n <= 0) break;
        if (!bin) {
            if (n < (int)sizeof buf) { buf[n] = 0; if (strstr((const char*)buf, "\"kick\"")) g_kicked = true; }
            continue;
        }
        if (buf[0] == 0xA7 && n >= 1 + CAR_COUNT * 10) {
            for (int i = 0; i < CAR_COUNT; ++i) {
                const uint8_t* q = buf + 1 + i * 10;
                int k = 0;
                for (; k < 10 && q[k]; ++k) g_names[i][k] = (char)q[k];
                g_names[i][k] = 0;
            }
            continue;
        }
        if (buf[0] == 0xA6 && n >= 6) {
            g_info.haveWelcome = true;
            g_info.slot = buf[1] & 7;
            g_info.laps = buf[2]; g_info.diff = buf[3] & 3; g_info.track = buf[4];
            g_info.host = buf[5] & 1;
        } else if (buf[0] == 0xA5 && n >= 4 + CAR_COUNT * 20) {
            g_info.haveSnap = true;
            g_info.phase = buf[1] & 3;
            g_info.track = (buf[1] >> 2) & 15;
            g_info.laps = kLapsTab[(buf[1] >> 6) & 3];
            g_info.raceTime = (buf[2] | (buf[3] << 8)) / 100.0f;
            uint8_t human = 0, ret = 0;
            for (int i = 0; i < CAR_COUNT; ++i) {
                const uint8_t* p = buf + 4 + i * 20;
                if (p[0] != 0xA4) continue;
                RCar& c = g_cars[i];
                uint8_t fl = p[1] >> 3;
                c.flags = fl;
                c.dist = rdf(p + 2); c.x = rdf(p + 6); c.speed = rdf(p + 10);
                c.steer = rd16(p + 14) / 1000.0f;
                c.place = p[18];
                if (fl & 16) human |= 1 << i;
                if (fl & 8) ret |= 1 << i;
            }
            if (g_info.haveSnap && g_info.phase == PH_RACING) {          // a player left during the race
                uint8_t dropped = g_prevHuman & ~human & ~(1 << g_info.slot);
                if (dropped) {
                    g_leftPending |= dropped;
                    for (int i = 0; i < CAR_COUNT; ++i) if ((dropped >> i) & 1) { snprintf(g_leftName, sizeof g_leftName, "%s", g_names[i][0] ? g_names[i] : "PLAYER"); break; }
                }
            }
            g_prevHuman = human;
            g_info.humanMask = human; g_info.retiredMask = ret;
            ++g_info.snapSeq;
        }
    }
}

bool kicked() { return g_kicked; }
bool takeLeft(char* out, size_t cap) {
    if (!g_leftPending) return false;
    snprintf(out, cap, "%s", g_leftName);
    g_leftPending = 0;
    return true;
}

// put the players' names on the race cars (lobby, tower, results); other cars keep their own name
void applyNames(Race& r) {
    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = r.cars[i];
        bool mine = c.name >= &g_names[0][0] && c.name < &g_names[0][0] + sizeof g_names;
        if (!mine) g_def[i] = c.name;
        bool human = (g_info.humanMask >> i) & 1;
        if (human && g_names[i][0]) c.name = g_names[i];
        else if (mine && g_def[i]) c.name = g_def[i];
    }
}

void sendConfigure(int track, int laps, int diff) {
    char b[120];
    snprintf(b, sizeof b, "{\"type\":\"room:configure\",\"payload\":{\"trackId\":%d,\"laps\":%d,\"difficulty\":%d}}", track, laps, diff);
    send_json(b);
}
void sendStart() { send_json("{\"type\":\"race:start\"}"); }
void sendLobby() { send_json("{\"type\":\"race:lobby\"}"); }

void sendTelemetry(const Race& r) {
    uint8_t p[20];
    const Car& c = r.cars[g_info.slot];
    int fl = 0;
    if (c.braking) fl |= 1;
    if (r.nitroOn) fl |= 2;
    if (r.pitting) fl |= 4;
    if (c.retired) fl |= 8;
    p[0] = 0xA4;
    p[1] = (uint8_t)((g_info.slot & 7) | (fl << 3));
    wrf(p + 2, c.dist); wrf(p + 6, c.x); wrf(p + 10, c.speed);
    int16_t st = (int16_t)(c.steerVis * 1000.0f);
    p[14] = (uint8_t)st; p[15] = (uint8_t)(st >> 8);
    uint16_t dm = (uint16_t)(r.damage * 1000.0f);
    p[16] = (uint8_t)dm; p[17] = (uint8_t)(dm >> 8);
    ++g_seq;
    p[18] = (uint8_t)g_seq; p[19] = (uint8_t)(g_seq >> 8);
    plat::netSend(true, p, sizeof p);
}

void feed(Race& r) {
    bool fresh = g_info.snapSeq != g_fedSeq;
    g_fedSeq = g_info.snapSeq;
    for (int i = 0; i < CAR_COUNT; ++i) {
        Car& c = r.cars[i];
        if (!c.remote || !g_info.haveSnap) continue;
        const RCar& n = g_cars[i];
        if (fresh) {
            c.netDist = n.dist; c.netX = n.x; c.netSpeed = n.speed; c.netSteer = n.steer; c.netAge = 0;
            c.braking = n.flags & 1;
            c.retired = (n.flags & 8) != 0;
        }
    }
}

} // namespace online
