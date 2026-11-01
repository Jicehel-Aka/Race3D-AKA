// ws.cpp - minimal WebSocket client, see ws.h
#include "ws.h"
#include <stdio.h>
#include <string.h>

namespace {

// read exactly n bytes (each wait is bounded by timeoutMs); 1 = ok, 0 = timeout before the first byte, -1 = error / closed
int read_full(const WsIO& io, uint8_t* p, int n, int timeoutMs, bool first) {
    int got = 0, stalls = 0;
    while (got < n) {
        int r = io.read(io.ctx, p + got, n - got, timeoutMs);
        if (r < 0) return -1;
        if (r == 0) { if (got == 0 && first) return 0; if (++stalls > 5) return -1; continue; }   // mid-frame stall: wait a bit, then give up
        got += r;
    }
    return 1;
}

bool write_full(const WsIO& io, const uint8_t* p, int n) {
    int done = 0;
    while (done < n) {
        int w = io.write(io.ctx, p + done, n - done);
        if (w <= 0) return false;
        done += w;
    }
    return true;
}

uint32_t next_rand(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

bool send_frame(const WsIO& io, uint8_t opcode, const uint8_t* data, int len, uint32_t seed) {
    if (len < 0 || len > 1000) return false;
    uint8_t out[1020];
    int h = 0;
    out[h++] = 0x80 | opcode;
    if (len < 126) out[h++] = 0x80 | (uint8_t)len;
    else { out[h++] = 0x80 | 126; out[h++] = (uint8_t)(len >> 8); out[h++] = (uint8_t)len; }
    uint32_t r = next_rand(seed);
    uint8_t mask[4] = { (uint8_t)r, (uint8_t)(r >> 8), (uint8_t)(r >> 16), (uint8_t)(r >> 24) };
    for (int i = 0; i < 4; ++i) out[h++] = mask[i];
    for (int i = 0; i < len; ++i) out[h + i] = data[i] ^ mask[i & 3];
    return write_full(io, out, h + len);   // one write: TCP never sends the header alone
}

} // namespace

bool ws_handshake(const WsIO& io, const char* host, const char* path, uint32_t seed) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t raw[16];
    for (int i = 0; i < 16; ++i) raw[i] = (uint8_t)(next_rand(seed) >> 24);
    char key[32]; int k = 0;
    for (int i = 0; i < 15; i += 3) {
        uint32_t v = (raw[i] << 16) | (raw[i + 1] << 8) | raw[i + 2];
        key[k++] = b64[(v >> 18) & 63]; key[k++] = b64[(v >> 12) & 63]; key[k++] = b64[(v >> 6) & 63]; key[k++] = b64[v & 63];
    }
    key[k++] = b64[(raw[15] >> 2) & 63]; key[k++] = b64[(raw[15] << 4) & 63]; key[k++] = '='; key[k++] = '='; key[k] = 0;
    char req[480];
    int n = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
        path, host, key);
    if (n <= 0 || n >= (int)sizeof req) return false;
    if (!write_full(io, (const uint8_t*)req, n)) return false;
    // read the response until the blank line
    char resp[512]; int len = 0;
    for (int waited = 0; waited < 15000;) {
        uint8_t c;
        int r = io.read(io.ctx, &c, 1, 250);
        if (r < 0) return false;
        if (r == 0) { waited += 250; continue; }
        if (len < (int)sizeof resp - 1) resp[len++] = (char)c;
        if (len >= 4 && memcmp(resp + len - 4, "\r\n\r\n", 4) == 0) { resp[len] = 0; return strstr(resp, " 101 ") != nullptr; }
    }
    return false;
}

bool ws_send(const WsIO& io, bool binary, const uint8_t* data, int len, uint32_t maskSeed) {
    return send_frame(io, binary ? 0x2 : 0x1, data, len, maskSeed);
}

bool ws_send_ping(const WsIO& io, uint32_t maskSeed) { return send_frame(io, 0x9, nullptr, 0, maskSeed); }

int ws_recv(const WsIO& io, uint8_t* buf, int cap, bool* binary, int timeoutMs, uint32_t maskSeed) {
    for (;;) {
        uint8_t h[2];
        int r = read_full(io, h, 2, timeoutMs, true);
        if (r <= 0) return r;
        uint8_t op = h[0] & 0x0F;
        bool masked = h[1] & 0x80;
        uint32_t len = h[1] & 0x7F;
        if (len == 126) { uint8_t e[2]; if (read_full(io, e, 2, 1000, false) < 0) return -1; len = (e[0] << 8) | e[1]; }
        else if (len == 127) { uint8_t e[8]; if (read_full(io, e, 8, 1000, false) < 0) return -1; if (e[0] | e[1] | e[2] | e[3] | e[4]) return -1; len = ((uint32_t)e[4] << 24) | (e[5] << 16) | (e[6] << 8) | e[7]; }
        uint8_t mk[4] = { 0, 0, 0, 0 };
        if (masked && read_full(io, mk, 4, 1000, false) < 0) return -1;
        bool keep = (op == 0x1 || op == 0x2 || op == 0x0) && len <= (uint32_t)cap && (h[0] & 0x80);   // only whole, small data messages are kept
        uint8_t small[128];
        uint32_t got = 0;
        while (got < len) {                                   // always consume the payload, even when dropped
            uint8_t* dst = keep ? buf + got : small;
            int chunk = keep ? (int)(len - got) : (int)((len - got) < sizeof small ? (len - got) : sizeof small);
            if (read_full(io, dst, chunk, 1000, false) < 0) return -1;
            if (masked && keep) for (int i = 0; i < chunk; ++i) dst[i] ^= mk[(got + i) & 3];
            got += chunk;
        }
        if (op == 0x8) return -1;                             // close
        if (op == 0x9) {                                      // ping -> pong with the same payload
            if (len <= sizeof small) { /* payload already dropped: empty pong is accepted by servers */ }
            send_frame(io, 0xA, nullptr, 0, maskSeed);
            continue;
        }
        if (op == 0xA) continue;                              // pong
        if (!keep) return 0;
        if (binary) *binary = (op == 0x2);
        return (int)len == 0 ? 0 : (int)len;
    }
}
