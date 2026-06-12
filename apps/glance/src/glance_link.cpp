// Transport pump for glance: USB serial + hub HTTP only — no BLE, no demo
// mode. Each line is tried as a command first, else applied as an agent
// snapshot into the shared store.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include "line_bus.h"
#include "transport_usb.h"
#include "transport_http.h"
#include "agent_state.h"
#include "hw/rtc.h"
#include "glance_state.h"

// The app supplies entropy for the lib's matrixify filter.
extern "C" uint32_t agentStateRandom() { return esp_random(); }

static uint32_t _lastLiveMs = 0;

bool dataConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

// Set true once a bridge/hub sends a time sync — until then the RTC may
// hold whatever was on the coin cell (or 2000-01-01 if it lost power).
static bool _rtcValid = false;
bool dataRtcValid() { return _rtcValid; }

static void _applyLine(const char* line, TamaState* out) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (glanceCommand(doc)) { _lastLiveMs = millis(); return; }

  AgentEvent ev = agentApplyJson(doc, *out, millis());

  if (ev.kind == AgentEvent::TIME_SYNC) {
    // gmtime_r on the tz-adjusted epoch yields local components incl weekday.
    time_t local = (time_t)ev.epoch + ev.tzOffsetSec;
    struct tm lt; gmtime_r(&local, &lt);
    HwTime ht;
    ht.H  = lt.tm_hour;
    ht.M  = lt.tm_min;
    ht.S  = lt.tm_sec;
    ht.Y  = lt.tm_year + 1900;
    ht.Mo = lt.tm_mon + 1;
    ht.D  = lt.tm_mday;
    ht.dow = lt.tm_wday;
    hwRtcWrite(ht);
    // The status screen's RTC cache refreshes at 1 Hz, so the clock row
    // agrees with _rtcValid within a second of the sync — no forced re-read.
    _rtcValid = true;
    _lastLiveMs = millis();
    return;
  }

  if (ev.kind == AgentEvent::STATE_UPDATED) _lastLiveMs = millis();
}

// Pump a ByteSource through a framer; lines starting with '{' get applied.
template<size_t N>
static void _pump(ByteSource& src, LineFramer<N>& f, TamaState* out) {
  while (src.available(src.ctx)) {
    int c = src.read(src.ctx);
    if (c < 0) break;
    const char* line = f.feed((char)c);
    if (line && line[0] == '{') _applyLine(line, out);
  }
}

static LineFramer<1024> _usbLine, _httpLine;

void dataPoll(TamaState* out) {
  _pump(usbSerialSource(), _usbLine, out);
  _pump(httpByteSource(), _httpLine, out);

  out->connected = dataConnected();
  if (!out->connected) {
    out->sessionsTotal = 0; out->sessionsRunning = 0; out->sessionsWaiting = 0;
    out->recentlyCompleted = false; out->lastUpdated = millis();
    strncpy(out->msg, "no agent data", sizeof(out->msg) - 1);
    out->msg[sizeof(out->msg) - 1] = 0;
  }
}
