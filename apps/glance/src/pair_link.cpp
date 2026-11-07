// notify pairing over Wi-Fi: a task runs the HTTP (POST /api/devices/pair,
// then GET /api/devices/pair/{id} every interval) against lib/notify-pair's
// state machine; the loop reads a view and adopts the token once approved.
// https verifies against the IDF CA bundle, as wss does (ADR 014).
#include <Arduino.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_mac.h>
#include "wifi_link.h"
#include "glance_state.h"

static const uint32_t APPROVED_SHOW_MS = 4000;

// _s is shared with the task; JSON parsing allocates, so a mutex, not a spinlock.
static SemaphoreHandle_t _lock = nullptr;
static PairSession _s;
static uint32_t _gen = 0;
static bool _taskRunning = false;
static uint32_t _approvedAt = 0;

struct Locked {
  Locked()  { xSemaphoreTake(_lock, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(_lock); }
};

// One request with fixed buffers; returns the HTTP status or -1.
static int _http(const char* url, const char* body, const char* bearer, char* resp, size_t cap) {
  esp_http_client_config_t c = {};
  c.url = url;
  c.method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  c.timeout_ms = 10000;
  if (!strncmp(url, "https://", 8)) c.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) return -1;
  if (body) esp_http_client_set_header(h, "Content-Type", "application/json");
  if (bearer) {
    char auth[100];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer);
    esp_http_client_set_header(h, "Authorization", auth);
  }
  int len = body ? (int)strlen(body) : 0;
  int status = -1;
  resp[0] = 0;
  if (esp_http_client_open(h, len) == ESP_OK &&
      (len == 0 || esp_http_client_write(h, body, len) == len) &&
      esp_http_client_fetch_headers(h) >= 0) {
    status = esp_http_client_get_status_code(h);
    int n = 0, r;
    while (n < (int)cap - 1 && (r = esp_http_client_read(h, resp + n, cap - 1 - n)) > 0) n += r;
    resp[n] = 0;
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return status;
}

static void _pairBody(char* out, size_t cap) {
#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "dev"
#endif
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  snprintf(out, cap,
    "{\"model\":\"%s %s\",\"fw\":\"%s\",\"hw\":\"%02x:%02x:%02x:%02x:%02x:%02x\"}",
    BOARD_MODEL_LINE1, BOARD_MODEL_LINE2, BUDDY_FW_VERSION, m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void _pairTask(void*) {
  static char url[180], secret[80], body[160], resp[1024];
  for (;;) {
    uint32_t gen;
    PairPhase phase;
    bool due;
    {
      Locked l;
      phase = _s.phase;
      if (phase != PAIR_REQUESTING && phase != PAIR_WAITING) { _taskRunning = false; break; }
      pairCheckDeadline(_s, millis());
      gen = _gen;
      due = phase == PAIR_REQUESTING || pairPollDue(_s, millis());
      if (phase == PAIR_REQUESTING) snprintf(url, sizeof(url), "%s/api/devices/pair", _s.base);
      else snprintf(url, sizeof(url), "%s/api/devices/pair/%s", _s.base, _s.pairingId);
      strcpy(secret, _s.secret);
    }
    if (!due || wifiLinkState() != WIFI_LINK_ONLINE) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    int status;
    if (phase == PAIR_REQUESTING) { _pairBody(body, sizeof(body)); status = _http(url, body, nullptr, resp, sizeof(resp)); }
    else status = _http(url, nullptr, secret, resp, sizeof(resp));
    memset(secret, 0, sizeof(secret));
    Serial.printf("[pair] %s → %d\n", phase == PAIR_REQUESTING ? "pair" : "poll", status);

    Locked l;
    if (_gen != gen) continue;   // cancelled or restarted mid-request
    if (phase == PAIR_REQUESTING) pairOnCreated(_s, status, resp, millis());
    else pairOnPoll(_s, status, resp, millis());
  }
  memset(resp, 0, sizeof(resp));
  vTaskDelete(nullptr);
}

bool pairLinkStart(const char* wsUrl, const char* console) {
  if (!_lock) _lock = xSemaphoreCreateMutex();
  bool ok, spawn;
  {
    Locked l;
    ok = pairStart(_s, wsUrl, console);
    if (!ok) pairWipe(_s);
    _gen++;
    _approvedAt = 0;
    spawn = ok && !_taskRunning;
    if (spawn) _taskRunning = true;
  }
  // TLS handshakes run on this stack, as in transport-ws.
  if (spawn) xTaskCreate(_pairTask, "pair_link", 8192, nullptr, 1, nullptr);
  return ok;
}

void pairLinkClose() {
  if (!_lock) return;
  Locked l;
  pairWipe(_s);
  _gen++;
}

PairView pairLinkView() {
  PairView v = {};
  if (!_lock) return v;
  Locked l;
  v.phase = _s.phase;
  v.waitingForWifi = _s.phase == PAIR_REQUESTING && wifiLinkState() != WIFI_LINK_ONLINE;
  v.secondsLeft = pairSecondsLeft(_s, millis());
  strcpy(v.code, _s.code);
  strcpy(v.host, _s.shownHost);
  strcpy(v.error, _s.error);
  return v;
}

void pairLinkTick() {
  if (!_lock) return;
  char token[sizeof(_s.token)], connect[sizeof(_s.connect)];
  {
    Locked l;
    if (_s.phase != PAIR_APPROVED) return;
    if (_approvedAt) {
      if (millis() - _approvedAt >= APPROVED_SHOW_MS) pairWipe(_s);
      return;
    }
    _approvedAt = millis();
    strcpy(token, _s.token);
    strcpy(connect, _s.connect);
    memset(_s.token, 0, sizeof(_s.token));
  }
  notifyLinkAdopt(connect, token);
  memset(token, 0, sizeof(token));
  Serial.println("[pair] approved: token stored, dialing");
}
