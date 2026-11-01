// ws.h - minimal WebSocket client (RFC 6455) over any blocking byte stream. Portable: used by the ESP32 network
// task and by the PC test client. Client frames are masked, messages are never fragmented on the way out.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct WsIO {
    void* ctx;
    // returns bytes read (> 0), 0 when the timeout elapsed with no data, < 0 when the stream is closed / broken
    int (*read)(void* ctx, uint8_t* buf, int n, int timeoutMs);
    // returns the bytes written (all of them) or < 0 on error
    int (*write)(void* ctx, const uint8_t* buf, int n);
};

bool ws_handshake(const WsIO& io, const char* host, const char* path, uint32_t seed);
// sends one message (opcode 0x1 text / 0x2 binary). false on error.
bool ws_send(const WsIO& io, bool binary, const uint8_t* data, int len, uint32_t maskSeed);
bool ws_send_ping(const WsIO& io, uint32_t maskSeed);
// reads one data message into buf (answers pings, drops oversized messages).
// returns the length (> 0), 0 when nothing arrived within timeoutMs (or the message was ignored), < 0 on close / error.
int  ws_recv(const WsIO& io, uint8_t* buf, int cap, bool* binary, int timeoutMs, uint32_t maskSeed);
