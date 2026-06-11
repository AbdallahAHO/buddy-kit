#include "transport_http.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include "wifi_link.h"

static char _url[96] = "";
static volatile bool _healthy = false;
static TaskHandle_t  _task = nullptr;

// Single-producer/single-consumer byte rings (32-bit index reads/writes are
// atomic, so no locks): http task fills RX / drains TX, loop does the inverse.
static const size_t RX_CAP = 2048;
static uint8_t  _rx[RX_CAP];
static volatile size_t _rxHead = 0, _rxTail = 0;
static const size_t TX_CAP = 1024;
static uint8_t  _tx[TX_CAP];
static volatile size_t _txHead = 0, _txTail = 0;

static const uint32_t POLL_MS = 1000;

static void _rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (_rxHead + 1) % RX_CAP;
    if (next == _rxTail) return;   // full — drop; next poll re-delivers state
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
static size_t _write(void*, const uint8_t* d, size_t n) {
  size_t written = 0;
  for (size_t i = 0; i < n; i++) {
    size_t next = (_txHead + 1) % TX_CAP;
    if (next == _txTail) break;
    _tx[_txHead] = d[i];
    _txHead = next;
    written++;
  }
  return written;
}

ByteSource& httpByteSource() {
  static ByteSource s = { _avail, _read, _write, nullptr };
  return s;
}

static void _pollTask(void*) {
  for (;;) {
    if (!_url[0] || wifiLinkState() != WIFI_LINK_ONLINE) {
      _healthy = false;
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
      continue;
    }

    HTTPClient http;
    bool ok = true;

    // Flush device → hub lines first (acks, permission decisions, status).
    size_t pending = (_txHead + TX_CAP - _txTail) % TX_CAP;
    if (pending > 0) {
      static uint8_t body[TX_CAP];
      size_t n = 0;
      while (_txTail != _txHead && n < sizeof(body)) {
        body[n++] = _tx[_txTail];
        _txTail = (_txTail + 1) % TX_CAP;
      }
      char url[112]; snprintf(url, sizeof(url), "%s/push", _url);
      if (http.begin(url)) {
        http.addHeader("Content-Type", "application/x-ndjson");
        int code = http.POST(body, n);
        ok = code > 0 && code < 300;
        http.end();
      } else ok = false;
    }

    // Pull hub → device lines.
    {
      char url[112]; snprintf(url, sizeof(url), "%s/poll", _url);
      if (http.begin(url)) {
        int code = http.GET();
        if (code == 200) {
          String body = http.getString();
          if (body.length()) _rxPush((const uint8_t*)body.c_str(), body.length());
        } else if (code != 204) ok = false;
        http.end();
      } else ok = false;
    }

    _healthy = ok;
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

void httpTransportStart(const char* baseUrl) {
  strncpy(_url, baseUrl, sizeof(_url)-1); _url[sizeof(_url)-1] = 0;
  // Drop a trailing slash so /poll concatenation stays clean.
  size_t len = strlen(_url);
  if (len && _url[len-1] == '/') _url[len-1] = 0;
  if (!_task)
    xTaskCreate(_pollTask, "http_poll", 6144, nullptr, 1, &_task);
  Serial.printf("[http] hub: %s\n", _url);
}

void httpTransportStop() {
  _url[0] = 0;
  _healthy = false;
}

bool httpTransportConfigured() { return _url[0] != 0; }
bool httpTransportHealthy()    { return _healthy; }
const char* httpTransportUrl() { return _url; }
