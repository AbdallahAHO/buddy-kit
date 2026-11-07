#include "transport_ws.h"
#include <Arduino.h>
#include <esp_random.h>
#include <esp_transport.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <esp_crt_bundle.h>
#include "wifi_link.h"

static const uint32_t PING_EVERY_MS   = 30000;
static const uint32_t SILENT_LIMIT_MS = 75000;
static const uint32_t STABLE_UP_MS    = 60000;
static const uint32_t BACKOFF_MIN_MS  = 1000;
static const uint32_t BACKOFF_MAX_MS  = 60000;
static const int      CONNECT_TIMEOUT_MS = 10000;
static const uint16_t CLOSE_REVOKED   = 4001;

// Config handoff: the loop writes under _mux and bumps _gen; the task copies
// on a gen change and drops any live session.
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static char _cfgUrl[128] = "";
static char _cfgTok[96] = "";
static volatile uint32_t _gen = 0;
static char _url[128] = "";          // loop-side copy for status
static volatile bool _connected = false;
static volatile bool _revoked = false;
static TaskHandle_t _task = nullptr;

// Single-producer/single-consumer rings, as in transport-http.
static const size_t RX_CAP = 2048;
static uint8_t _rx[RX_CAP];
static volatile size_t _rxHead = 0, _rxTail = 0;
static const size_t TX_CAP = 1024;
static uint8_t _tx[TX_CAP];
static volatile size_t _txHead = 0, _txTail = 0;

static void _rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (_rxHead + 1) % RX_CAP;
    if (next == _rxTail) return;
    _rx[_rxHead] = p[i];
    _rxHead = next;
  }
}

static size_t _avail(void*) { return (_rxHead + RX_CAP - _rxTail) % RX_CAP; }
static int _read(void*) {
  if (_rxHead == _rxTail) return -1;
  int b = _rx[_rxTail];
  _rxTail = (_rxTail + 1) % RX_CAP;
  return b;
}
// All-or-nothing, and only while the socket is up: a partial line would
// corrupt framing, and lines written offline would flush as stale replies.
static size_t _write(void*, const uint8_t* d, size_t n) {
  if (!_connected) return 0;
  size_t used = (_txHead + TX_CAP - _txTail) % TX_CAP;
  if (n > TX_CAP - 1 - used) return 0;
  for (size_t i = 0; i < n; i++) {
    _tx[_txHead] = d[i];
    _txHead = (_txHead + 1) % TX_CAP;
  }
  return n;
}

ByteSource& wsByteSource() {
  static ByteSource s = { _avail, _read, _write, nullptr };
  return s;
}

struct WsTarget {
  bool tls;
  char host[64];
  int  port;
  char path[96];
};

static bool _parseUrl(const char* url, WsTarget& t) {
  const char* p;
  if (!strncmp(url, "wss://", 6))     { t.tls = true;  t.port = 443; p = url + 6; }
  else if (!strncmp(url, "ws://", 5)) { t.tls = false; t.port = 80;  p = url + 5; }
  else return false;
  size_t h = strcspn(p, ":/");
  if (h == 0 || h >= sizeof(t.host)) return false;
  memcpy(t.host, p, h); t.host[h] = 0;
  p += h;
  if (*p == ':') { t.port = atoi(p + 1); p += 1 + strspn(p + 1, "0123456789"); }
  if (t.port <= 0 || t.port > 65535) return false;
  if (!*p) p = "/";
  if (*p != '/' || strlen(p) >= sizeof(t.path)) return false;
  strcpy(t.path, p);
  return true;
}

// Drains complete lines from the TX ring as text frames; false on a send error.
static bool _flushTx(esp_transport_handle_t ws) {
  static char line[TX_CAP];
  static size_t len = 0;
  while (_txTail != _txHead) {
    char c = (char)_tx[_txTail];
    _txTail = (_txTail + 1) % TX_CAP;
    if (c != '\n') { if (len < sizeof(line)) line[len++] = c; continue; }
    if (len && esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN),
                                         line, (int)len, 5000) < 0) {
      len = 0;
      return false;
    }
    len = 0;
  }
  return true;
}

struct SessionEnd {
  bool     wasUp;
  uint32_t upMs;
  uint16_t closeCode;   // 0 = no close frame
};

static SessionEnd _session(const char* url, const char* token, uint32_t gen) {
  SessionEnd end = { false, 0, 0 };
  WsTarget t;
  if (!_parseUrl(url, t)) { Serial.printf("[ws] bad url: %s\n", url); return end; }

  esp_transport_handle_t base = t.tls ? esp_transport_ssl_init() : esp_transport_tcp_init();
  if (!base) return end;
  if (t.tls) esp_transport_ssl_crt_bundle_attach(base, esp_crt_bundle_attach);
  esp_transport_handle_t ws = esp_transport_ws_init(base);
  if (!ws) { esp_transport_destroy(base); return end; }

  char headers[128];
  snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
  esp_transport_ws_config_t cfg = {};
  cfg.ws_path = t.path;
  cfg.headers = headers;
  cfg.propagate_control_frames = true;   // CLOSE must reach us to see 4001
  esp_transport_ws_set_config(ws, &cfg);
  memset(headers, 0, sizeof(headers));

  if (esp_transport_connect(ws, t.host, t.port, CONNECT_TIMEOUT_MS) < 0) {
    Serial.printf("[ws] connect failed (http %d)\n", esp_transport_ws_get_upgrade_request_status(ws));
  } else {
    uint32_t upAt = millis(), lastRx = upAt, lastPing = upAt;
    uint32_t got = 0;   // payload bytes consumed of the current frame
    _connected = end.wasUp = true;
    Serial.printf("[ws] up %s (stack free %u)\n", url, (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    for (;;) {
      if (_gen != gen || wifiLinkState() != WIFI_LINK_ONLINE) break;
      uint32_t now = millis();
      if (now - lastRx >= SILENT_LIMIT_MS) { Serial.println("[ws] no pong for 75 s"); break; }
      if (now - lastPing >= PING_EVERY_MS) {
        lastPing = now;
        if (esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN),
                                      "ping", 4, 5000) < 0) break;
      }
      if (!_flushTx(ws)) break;

      char buf[256];
      int r = esp_transport_read(ws, buf, sizeof(buf), 100);
      if (r < 0) break;
      if (r == 0) continue;
      lastRx = millis();
      int op = esp_transport_ws_get_read_opcode(ws) & 0x0F;
      int total = esp_transport_ws_get_read_payload_len(ws);
      got += r;
      bool complete = (int)got >= total;
      if (complete) got = 0;

      if (op == WS_TRANSPORT_OPCODES_CLOSE) {
        end.closeCode = r >= 2 ? (uint16_t)(((uint8_t)buf[0] << 8) | (uint8_t)buf[1]) : 1005;
        esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(WS_TRANSPORT_OPCODES_CLOSE | WS_TRANSPORT_OPCODES_FIN),
                                  buf, r >= 2 ? 2 : 0, 1000);
        Serial.printf("[ws] closed by server: %u\n", end.closeCode);
        break;
      }
      if (op == WS_TRANSPORT_OPCODES_PING) {
        esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(WS_TRANSPORT_OPCODES_PONG | WS_TRANSPORT_OPCODES_FIN),
                                  buf, r, 1000);
        continue;
      }
      if (op == WS_TRANSPORT_OPCODES_PONG) continue;
      _rxPush((const uint8_t*)buf, (size_t)r);
      if (complete && esp_transport_ws_get_fin_flag(ws)) _rxPush((const uint8_t*)"\n", 1);
    }
    _connected = false;
    end.upMs = millis() - upAt;
  }
  esp_transport_close(ws);
  esp_transport_destroy(ws);
  esp_transport_destroy(base);
  return end;
}

static uint32_t _backoffMs(uint8_t fails) {
  uint32_t ms = BACKOFF_MIN_MS << (fails > 6 ? 6 : fails);
  if (ms > BACKOFF_MAX_MS) ms = BACKOFF_MAX_MS;
  return ms * (80 + esp_random() % 41) / 100;   // ±20 % jitter
}

static void _wsTask(void*) {
  static char url[sizeof(_cfgUrl)], token[sizeof(_cfgTok)];
  uint32_t gen = _gen - 1;
  uint8_t fails = 0;
  uint32_t retryAt = 0;
  for (;;) {
    if (gen != _gen) {
      portENTER_CRITICAL(&_mux);
      memcpy(url, _cfgUrl, sizeof(url));
      memcpy(token, _cfgTok, sizeof(token));
      gen = _gen;
      portEXIT_CRITICAL(&_mux);
      fails = 0;
      retryAt = millis();
      _revoked = false;
    }
    if (!url[0] || _revoked || wifiLinkState() != WIFI_LINK_ONLINE ||
        (int32_t)(millis() - retryAt) < 0) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    SessionEnd end = _session(url, token, gen);
    if (gen != _gen) continue;   // reconfigured: connect again right away
    if (end.closeCode == CLOSE_REVOKED) {
      _revoked = true;
      Serial.println("[ws] revoked (4001): stopped until re-enrolled");
      continue;
    }
    if (end.wasUp && end.upMs >= STABLE_UP_MS) fails = 0;
    uint32_t wait = _backoffMs(fails);
    if (fails < 255) fails++;
    retryAt = millis() + wait;
    Serial.printf("[ws] retry in %lu ms\n", (unsigned long)wait);
  }
}

void wsTransportStart(const char* url, const char* token) {
  portENTER_CRITICAL(&_mux);
  strncpy(_cfgUrl, url, sizeof(_cfgUrl) - 1); _cfgUrl[sizeof(_cfgUrl) - 1] = 0;
  strncpy(_cfgTok, token ? token : "", sizeof(_cfgTok) - 1); _cfgTok[sizeof(_cfgTok) - 1] = 0;
  _gen = _gen + 1;
  portEXIT_CRITICAL(&_mux);
  strncpy(_url, url, sizeof(_url) - 1); _url[sizeof(_url) - 1] = 0;
  // TLS handshakes run on this stack; 8 KB covers mbedTLS with the bundle.
  if (!_task) xTaskCreate(_wsTask, "ws_link", 8192, nullptr, 1, &_task);
  Serial.printf("[ws] notify: %s\n", _url);
}

void wsTransportStop() {
  portENTER_CRITICAL(&_mux);
  _cfgUrl[0] = 0;
  memset(_cfgTok, 0, sizeof(_cfgTok));
  _gen = _gen + 1;
  portEXIT_CRITICAL(&_mux);
  _url[0] = 0;
}

bool wsTransportConfigured() { return _url[0] != 0; }
bool wsTransportConnected()  { return _connected; }
bool wsTransportRevoked()    { return _revoked; }
const char* wsTransportUrl() { return _url; }
