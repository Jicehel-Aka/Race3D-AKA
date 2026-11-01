// net_esp.cpp - Wi-Fi station + (TLS) TCP + WebSocket for the online races, on the ESP32-S3.
// One background task owns everything; the game thread only exchanges small messages through two ring buffers.
#include "game/platform.h"
#include "game/ws.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

namespace plat {
namespace {

const char* TAG = "net";

constexpr int SEND_SLOTS = 16, SEND_MAX = 160;
constexpr int RECV_SLOTS = 6, RECV_MAX = 300;

struct Msg { uint16_t len; bool bin; uint8_t data[RECV_MAX]; };

NetConfig            s_cfg;
volatile int         s_status = NET_OFF;
char                 s_err[32] = "";
volatile bool        s_stop = false;
volatile bool        s_running = false;
SemaphoreHandle_t    s_lock = nullptr;
esp_netif_t*         s_netif = nullptr;
volatile int         s_wifiState = 0;      // 0 waiting, 1 got IP, -1 failed
volatile int         s_retries = 0;

struct Ring { Msg* m; int head, tail, count, slots; };
Ring s_tx{}, s_rx{};

void ring_init(Ring& r, int slots) {
    if (!r.m) r.m = (Msg*)heap_caps_calloc(slots, sizeof(Msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    r.slots = slots; r.head = r.tail = r.count = 0;
}
bool ring_push(Ring& r, bool bin, const void* d, size_t n, size_t max, bool overwrite) {
    if (!r.m || n > max) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = true;
    if (r.count == r.slots) {
        if (!overwrite) ok = false;
        else { r.tail = (r.tail + 1) % r.slots; r.count--; }
    }
    if (ok) {
        Msg& m = r.m[r.head];
        m.len = (uint16_t)n; m.bin = bin; memcpy(m.data, d, n);
        r.head = (r.head + 1) % r.slots; r.count++;
    }
    xSemaphoreGive(s_lock);
    return ok;
}
int ring_pop(Ring& r, void* buf, size_t cap, bool* bin) {
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (r.count > 0) {
        Msg& m = r.m[r.tail];
        if (m.len <= cap) { memcpy(buf, m.data, m.len); if (bin) *bin = m.bin; n = m.len; }
        r.tail = (r.tail + 1) % r.slots; r.count--;
    }
    xSemaphoreGive(s_lock);
    return n;
}

void fail(const char* why) {
    snprintf(s_err, sizeof s_err, "%s", why);
    s_status = NET_FAILED;
}

// ------------------------------------------------------------------ Wi-Fi
void wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifiState == 0 && !s_stop && s_retries < 3) { s_retries = s_retries + 1; esp_wifi_connect(); }
        else if (s_wifiState == 0) s_wifiState = -1;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifiState = 1;
    }
}

bool wifi_up() {
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) { ESP_LOGW(TAG, "netif_init %d", e); }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    if (!s_netif) s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    wc.nvs_enable = 0;                      // no flash storage needed, saves RAM
    if (esp_wifi_init(&wc) != ESP_OK) return false;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t c = {};
    strncpy((char*)c.sta.ssid, s_cfg.ssid, sizeof c.sta.ssid - 1);
    strncpy((char*)c.sta.password, s_cfg.pass, sizeof c.sta.password - 1);
    c.sta.threshold.authmode = s_cfg.pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    c.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    s_wifiState = 0; s_retries = 0;
    return esp_wifi_start() == ESP_OK;
}

void wifi_down() {
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event);
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
}

// ------------------------------------------------------------------ stream (TLS or plain TCP)
struct Conn {
    esp_tls_t* tls = nullptr;
    int fd = -1;
    bool secure = false;
};

bool conn_open(Conn& c) {
    c.secure = s_cfg.tls;
    if (c.secure) {
        c.tls = esp_tls_init();
        if (!c.tls) return false;
        esp_tls_cfg_t tc = {};
        tc.crt_bundle_attach = esp_crt_bundle_attach;
        tc.timeout_ms = 12000;
        if (esp_tls_conn_new_sync(s_cfg.host, (int)strlen(s_cfg.host), s_cfg.port, &tc, c.tls) != 1) return false;
        esp_tls_get_conn_sockfd(c.tls, &c.fd);
        return true;
    }
    addrinfo hints = {}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char port[8]; snprintf(port, sizeof port, "%u", (unsigned)s_cfg.port);
    if (getaddrinfo(s_cfg.host, port, &hints, &res) != 0 || !res) return false;
    c.fd = socket(res->ai_family, res->ai_socktype, 0);
    bool ok = c.fd >= 0 && connect(c.fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok && c.fd >= 0) { close(c.fd); c.fd = -1; }
    return ok;
}

void conn_close(Conn& c) {
    if (c.tls) { esp_tls_conn_destroy(c.tls); c.tls = nullptr; c.fd = -1; }
    else if (c.fd >= 0) { close(c.fd); c.fd = -1; }
}

int conn_read(void* ctx, uint8_t* buf, int n, int timeoutMs) {
    Conn* c = (Conn*)ctx;
    if (s_stop) return -1;
    if (!(c->tls && esp_tls_get_bytes_avail(c->tls) > 0)) {
        fd_set rs; FD_ZERO(&rs); FD_SET(c->fd, &rs);
        timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        int r = select(c->fd + 1, &rs, nullptr, nullptr, &tv);
        if (r < 0) return -1;
        if (r == 0) return 0;
    }
    int k = c->tls ? (int)esp_tls_conn_read(c->tls, buf, n) : (int)recv(c->fd, buf, n, 0);
    if (k > 0) return k;
    if (k == 0) return -1;                                  // closed by the server
    if (c->tls && (k == ESP_TLS_ERR_SSL_WANT_READ || k == ESP_TLS_ERR_SSL_WANT_WRITE)) return 0;
    if (!c->tls && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return -1;
}

int conn_write(void* ctx, const uint8_t* buf, int n) {
    Conn* c = (Conn*)ctx;
    int done = 0;
    while (done < n) {
        int k = c->tls ? (int)esp_tls_conn_write(c->tls, buf + done, n - done) : (int)send(c->fd, buf + done, n - done, 0);
        if (k > 0) { done += k; continue; }
        if (c->tls && (k == ESP_TLS_ERR_SSL_WANT_READ || k == ESP_TLS_ERR_SSL_WANT_WRITE)) { vTaskDelay(1); continue; }
        if (!c->tls && (errno == EAGAIN || errno == EWOULDBLOCK)) { vTaskDelay(1); continue; }
        return -1;
    }
    return done;
}

// ------------------------------------------------------------------ the task
void net_task(void*) {
    s_running = true;
    bool wifi = false;
    Conn conn;
    do {
        s_status = NET_WIFI;
        if (!s_cfg.ssid[0]) { fail("NO WIFI NAME"); break; }
        wifi = wifi_up();
        if (!wifi) { fail("WIFI START"); break; }
        for (int i = 0; i < 400 && s_wifiState == 0 && !s_stop; ++i) vTaskDelay(pdMS_TO_TICKS(50));
        if (s_stop) break;
        if (s_wifiState != 1) { fail("WIFI: CANNOT JOIN"); break; }

        s_status = NET_CONNECTING;
        if (!conn_open(conn)) { fail(s_cfg.tls ? "SERVER / TLS" : "SERVER"); break; }
        WsIO io{ &conn, conn_read, conn_write };
        char hostHdr[110];
        if ((s_cfg.tls && s_cfg.port == 443) || (!s_cfg.tls && s_cfg.port == 80)) snprintf(hostHdr, sizeof hostHdr, "%s", s_cfg.host);
        else snprintf(hostHdr, sizeof hostHdr, "%s:%u", s_cfg.host, (unsigned)s_cfg.port);
        if (!ws_handshake(io, hostHdr, s_cfg.path, esp_random())) { fail("HANDSHAKE"); break; }
        s_status = NET_ONLINE;
        ESP_LOGI(TAG, "online, heap %u internal %u", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_free_internal_heap_size());

        int64_t lastPing = esp_timer_get_time();
        uint8_t buf[RECV_MAX];
        uint8_t out[SEND_MAX];
        while (!s_stop) {
            bool bin;
            int n;
            int sent = 0;
            while (sent < 8 && (n = ring_pop(s_tx, out, sizeof out, &bin)) > 0) {
                if (!ws_send(io, bin, out, n, esp_random())) { fail("LINK LOST"); goto done; }
                ++sent;
            }
            n = ws_recv(io, buf, sizeof buf, &bin, 15, esp_random());
            if (n < 0) { fail("LINK LOST"); break; }
            if (n > 0) ring_push(s_rx, bin, buf, n, RECV_MAX, true);
            int64_t now = esp_timer_get_time();
            if (now - lastPing > 10000000) { ws_send_ping(io, esp_random()); lastPing = now; }
        }
    } while (false);
done:
    conn_close(conn);
    if (wifi) wifi_down();
    if (s_status != NET_FAILED) s_status = NET_OFF;
    s_running = false;
    vTaskDelete(nullptr);
}

} // namespace

void netStart(const NetConfig& cfg) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < 300 && s_running; ++i) { s_stop = true; vTaskDelay(pdMS_TO_TICKS(50)); }   // previous task still closing
    if (s_running) return;
    s_cfg = cfg;
    s_stop = false;
    s_err[0] = 0;
    ring_init(s_tx, SEND_SLOTS);
    ring_init(s_rx, RECV_SLOTS);
    if (!s_tx.m || !s_rx.m) { fail("NO MEMORY"); return; }
    s_status = NET_WIFI;
    if (xTaskCreatePinnedToCore(net_task, "net", 10240, nullptr, 4, nullptr, 0) != pdPASS) fail("TASK");
}

void netStop() {
    s_stop = true;
    for (int i = 0; i < 100 && s_running; ++i) vTaskDelay(pdMS_TO_TICKS(30));
    if (!s_running && s_status != NET_FAILED) s_status = NET_OFF;
}

int netStatus() { return s_status; }
const char* netError() { return s_err; }

bool netSend(bool binary, const void* data, size_t len) {
    if (s_status != NET_ONLINE || !s_lock) return false;
    return ring_push(s_tx, binary, data, len, SEND_MAX, false);
}

int netRecv(void* buf, size_t cap, bool* binary) {
    if (!s_lock || !s_rx.m) return 0;
    return ring_pop(s_rx, buf, cap, binary);
}

bool readTextFile(const char* path, char* buf, size_t cap) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return true;
}

} // namespace plat
