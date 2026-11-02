// net_pc.cpp - network backend of the PC build: TCP (+ TLS through OpenSSL) + WebSocket, in a background thread.
// Same contract as the console's net_esp.cpp (see platform.h). Linux and Windows (MSYS2 / MinGW64, Winsock).
#include "game/platform.h"
#include "game/ws.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
static void sock_close(sock_t s) { closesocket(s); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
static void sock_close(sock_t s) { close(s); }
#endif

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif

namespace {

struct Msg { bool bin; std::vector<uint8_t> d; };
std::mutex g_mu;
std::deque<Msg> g_rx, g_tx;
std::atomic<int> g_status{plat::NET_OFF};
std::atomic<bool> g_stop{false};
plat::NetConfig g_cfg;
std::thread g_th;
char g_err[48] = "";

void set_err(const char* e) { snprintf(g_err, sizeof g_err, "%s", e); }

struct Conn {
    sock_t fd = SOCK_BAD;
#ifdef HAVE_OPENSSL
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
#endif
};

bool wait_readable(sock_t fd, int ms) {
    fd_set rs; FD_ZERO(&rs); FD_SET(fd, &rs);
    timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return select((int)fd + 1, &rs, nullptr, nullptr, &tv) > 0;
}

int io_read(void* c, uint8_t* buf, int n, int timeoutMs) {
    Conn* k = (Conn*)c;
#ifdef HAVE_OPENSSL
    if (k->ssl) {
        if (SSL_pending(k->ssl) == 0 && !wait_readable(k->fd, timeoutMs)) return 0;
        int r = SSL_read(k->ssl, buf, n);
        if (r > 0) return r;
        int e = SSL_get_error(k->ssl, r);
        return (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) ? 0 : -1;
    }
#endif
    if (!wait_readable(k->fd, timeoutMs)) return 0;
    int r = (int)recv(k->fd, (char*)buf, n, 0);
    return r > 0 ? r : -1;
}

int io_write(void* c, const uint8_t* buf, int n) {
    Conn* k = (Conn*)c;
    int done = 0;
    while (done < n) {
#ifdef HAVE_OPENSSL
        if (k->ssl) {
            int r = SSL_write(k->ssl, buf + done, n - done);
            if (r <= 0) { int e = SSL_get_error(k->ssl, r); if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) continue; return -1; }
            done += r;
            continue;
        }
#endif
        int r = (int)send(k->fd, (const char*)buf + done, n - done, 0);
        if (r <= 0) return -1;
        done += r;
    }
    return done;
}

#ifdef HAVE_OPENSSL
bool load_roots(SSL_CTX* ctx) {
#ifdef _WIN32
    X509_STORE* st = SSL_CTX_get_cert_store(ctx);
    HCERTSTORE hs = CertOpenSystemStoreA(0, "ROOT");
    if (!hs) return false;
    PCCERT_CONTEXT pc = nullptr;
    int n = 0;
    while ((pc = CertEnumCertificatesInStore(hs, pc)) != nullptr) {
        const unsigned char* p = pc->pbCertEncoded;
        X509* x = d2i_X509(nullptr, &p, (long)pc->cbCertEncoded);
        if (x) { if (X509_STORE_add_cert(st, x) == 1) ++n; X509_free(x); }
    }
    CertCloseStore(hs, 0);
    return n > 0;
#else
    return SSL_CTX_set_default_verify_paths(ctx) == 1;
#endif
}
#endif

void close_conn(Conn& k) {
#ifdef HAVE_OPENSSL
    if (k.ssl) { SSL_shutdown(k.ssl); SSL_free(k.ssl); k.ssl = nullptr; }
    if (k.ctx) { SSL_CTX_free(k.ctx); k.ctx = nullptr; }
#endif
    if (k.fd != SOCK_BAD) { sock_close(k.fd); k.fd = SOCK_BAD; }
}

void run() {
    g_status = plat::NET_CONNECTING;
    set_err("");
    Conn k;
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    char port[8]; snprintf(port, sizeof port, "%u", (unsigned)g_cfg.port);
    if (getaddrinfo(g_cfg.host, port, &hints, &res) != 0 || !res) { set_err("SERVER NOT FOUND"); g_status = plat::NET_FAILED; return; }
    for (addrinfo* a = res; a && k.fd == SOCK_BAD && !g_stop; a = a->ai_next) {
        sock_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == SOCK_BAD) continue;
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) k.fd = s; else sock_close(s);
    }
    freeaddrinfo(res);
    if (k.fd == SOCK_BAD) { if (!g_stop) { set_err("CANNOT REACH THE SERVER"); g_status = plat::NET_FAILED; } return; }
    int one = 1; setsockopt(k.fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, (const char*)&one, sizeof one);

    if (g_cfg.tls) {
#ifdef HAVE_OPENSSL
        k.ctx = SSL_CTX_new(TLS_client_method());
        if (!k.ctx || !load_roots(k.ctx)) { set_err("NO TLS CERTIFICATES"); g_status = plat::NET_FAILED; close_conn(k); return; }
        SSL_CTX_set_verify(k.ctx, SSL_VERIFY_PEER, nullptr);
        k.ssl = SSL_new(k.ctx);
        SSL_set_fd(k.ssl, (int)k.fd);
        SSL_set_tlsext_host_name(k.ssl, g_cfg.host);
        SSL_set1_host(k.ssl, g_cfg.host);
        if (SSL_connect(k.ssl) != 1) { set_err("TLS HANDSHAKE FAILED"); g_status = plat::NET_FAILED; close_conn(k); return; }
#else
        set_err("PC BUILD HAS NO TLS"); g_status = plat::NET_FAILED; close_conn(k); return;
#endif
    }

    WsIO io{&k, io_read, io_write};
    char hostHdr[128]; snprintf(hostHdr, sizeof hostHdr, "%s", g_cfg.host);
    uint32_t seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() | 1u;
    if (!ws_handshake(io, hostHdr, g_cfg.path, seed)) { set_err("SERVER REFUSED THE CONNECTION"); g_status = plat::NET_FAILED; close_conn(k); return; }
    g_status = plat::NET_ONLINE;

    uint8_t buf[320];
    auto lastPing = std::chrono::steady_clock::now();
    while (!g_stop) {
        for (;;) {
            Msg m;
            { std::lock_guard<std::mutex> l(g_mu); if (g_tx.empty()) break; m = std::move(g_tx.front()); g_tx.pop_front(); }
            if (!ws_send(io, m.bin, m.d.data(), (int)m.d.size(), ++seed)) { set_err("CONNECTION LOST"); g_status = plat::NET_FAILED; close_conn(k); return; }
        }
        bool bin = false;
        int n = ws_recv(io, buf, sizeof buf, &bin, 8, ++seed);
        if (n < 0) { if (!g_stop) { set_err("CONNECTION LOST"); g_status = plat::NET_FAILED; } break; }
        if (n > 0) {
            std::lock_guard<std::mutex> l(g_mu);
            g_rx.push_back({bin, std::vector<uint8_t>(buf, buf + n)});
            if (g_rx.size() > 6) g_rx.pop_front();
        }
        auto now = std::chrono::steady_clock::now();
        if (now - lastPing > std::chrono::seconds(20)) { lastPing = now; ws_send_ping(io, ++seed); }
    }
    close_conn(k);
    if (g_status != plat::NET_FAILED) g_status = plat::NET_OFF;
}

} // namespace

namespace plat {

void netStart(const NetConfig& cfg) {
    netStop();
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    { std::lock_guard<std::mutex> l(g_mu); g_rx.clear(); g_tx.clear(); }
    g_cfg = cfg;
    g_stop = false;
    g_status = NET_CONNECTING;
    g_th = std::thread(run);
}

void netStop() {
    g_stop = true;
    if (g_th.joinable()) g_th.join();
    g_status = NET_OFF;
}

int netStatus() { return g_status; }
const char* netError() { return g_err; }

bool netSend(bool binary, const void* data, size_t len) {
    if (g_status != NET_ONLINE) return false;
    std::lock_guard<std::mutex> l(g_mu);
    if (g_tx.size() >= 16) return false;
    g_tx.push_back({binary, std::vector<uint8_t>((const uint8_t*)data, (const uint8_t*)data + len)});
    return true;
}

int netRecv(void* buf, size_t cap, bool* binary) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_rx.empty()) return 0;
    Msg m = std::move(g_rx.front()); g_rx.pop_front();
    if (m.d.size() > cap) return 0;
    memcpy(buf, m.d.data(), m.d.size());
    *binary = m.bin;
    return (int)m.d.size();
}

} // namespace plat
