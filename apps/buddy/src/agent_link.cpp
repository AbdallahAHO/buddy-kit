#include "agent_link.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include "line_bus.h"
#include "transport_usb.h"
#include "transport_ble.h"
#include "transport_http.h"
#include "agent_state.h"
#include "hw/rtc.h"
#include "stats.h"
#include "app_commands.h"
#include "app_state.h"

// The app supplies entropy for the lib's matrixify filter.
extern "C" uint32_t agentStateRandom() { return esp_random(); }

// ---------------------------------------------------------------------------
// Three modes, checked in priority order:
//   demo   → auto-cycle fake scenarios every 8s, ignore live data
//   live   → JSON arrived in the last 30s over USB or BT
//   asleep → no data, all zeros, "No Claude connected"
// ---------------------------------------------------------------------------

static uint32_t _lastLiveMs = 0;
static uint32_t _lastBtByteMs = 0;   // hasClient() lies; track actual BT traffic
static bool     _demoMode   = false;
static uint8_t  _demoIdx    = 0;
static uint32_t _demoNext   = 0;

struct _Fake { const char* n; uint8_t t,r,w; bool c; uint32_t tok; };
static const _Fake _FAKES[] = {
  {"asleep",0,0,0,false,0}, {"one idle",1,0,0,false,12000},
  {"busy",4,3,0,false,89000}, {"attention",2,1,1,false,45000},
  {"completed",1,0,0,true,142000},
};

void dataSetDemo(bool on) {
  _demoMode = on;
  if (on) { _demoIdx = 0; _demoNext = millis(); }
}
bool dataDemo() { return _demoMode; }

bool dataConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

bool dataBtActive() {
  // Desktop's idle keepalive is ~10s; give it 1.5x headroom.
  return _lastBtByteMs != 0 && (millis() - _lastBtByteMs) <= 15000;
}

const char* dataScenarioName() {
  if (_demoMode) return _FAKES[_demoIdx].n;
  if (dataConnected()) return dataBtActive() ? "bt" : "usb";
  return "none";
}

// Set true once the bridge sends a time sync — until then the RTC may
// hold whatever was on the coin cell (or 2000-01-01 if it lost power).
static bool _rtcValid = false;
bool dataRtcValid() { return _rtcValid; }

static void _applyLine(const char* line, TamaState* out) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (appCommand(doc)) { _lastLiveMs = millis(); return; }

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
    _clkLastRead = 0;   // force re-read so the clock screen and _rtcValid agree
    _rtcValid = true;
    _lastLiveMs = millis();
    return;
  }

  if (ev.kind == AgentEvent::STATE_UPDATED) {
    if (ev.hasTokens) statsOnBridgeTokens(ev.tokens);
    _lastLiveMs = millis();
  }
}

// Pump a ByteSource through a framer; lines starting with '{' get applied.
template<size_t N>
static size_t _pump(ByteSource& src, LineFramer<N>& f, TamaState* out) {
  size_t consumed = 0;
  while (src.available(src.ctx)) {
    int c = src.read(src.ctx);
    if (c < 0) break;
    consumed++;
    const char* line = f.feed((char)c);
    if (line && line[0] == '{') _applyLine(line, out);
  }
  return consumed;
}

static LineFramer<1024> _usbLine, _btLine, _httpLine;

void dataPoll(TamaState* out) {
  uint32_t now = millis();

  if (_demoMode) {
    if (now >= _demoNext) { _demoIdx = (_demoIdx + 1) % 5; _demoNext = now + 8000; }
    const _Fake& s = _FAKES[_demoIdx];
    out->sessionsTotal=s.t; out->sessionsRunning=s.r; out->sessionsWaiting=s.w;
    out->recentlyCompleted=s.c; out->tokensToday=s.tok; out->lastUpdated=now;
    out->connected = true;
    snprintf(out->msg, sizeof(out->msg), "demo: %s", s.n);
    return;
  }

  _pump(usbSerialSource(), _usbLine, out);
  if (_pump(bleByteSource(), _btLine, out) > 0) _lastBtByteMs = millis();
  _pump(httpByteSource(), _httpLine, out);

  out->connected = dataConnected();
  if (!out->connected) {
    out->sessionsTotal=0; out->sessionsRunning=0; out->sessionsWaiting=0;
    out->recentlyCompleted=false; out->lastUpdated=now;
    strncpy(out->msg, "No Claude connected", sizeof(out->msg)-1);
    out->msg[sizeof(out->msg)-1]=0;
  }
}
