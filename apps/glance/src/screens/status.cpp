// The one glance surface: a full-screen status panel repainted every frame.
// It owns the whole canvas and never closes, so the ghost rule's
// full-clear-on-close path doesn't apply (AGENTS.md rule 3). Reads state,
// never writes it.
#include <Arduino.h>
#include "../glance_state.h"
#include "ui_canvas.h"
#include "wifi_link.h"
#include "transport_http.h"
#include "hw/hw.h"

// faces is excluded from this composition, so no characterPalette() —
// the panel owns its colorway: black bg, white text, grey dim, green body.
static const Palette PAL = {
  /*body*/ 0x07E0, /*bg*/ 0x0000, /*text*/ 0xFFFF, /*textDim*/ 0x8410,
  /*ink*/ 0xFFFF,
};

// Screen-local RTC cache: the PCF85063 sits on the shared I2C bus, so read
// it at 1 Hz from the tick, not on every frame.
static HwTime   _tm;
static uint32_t _tmReadAt = 0;

void statusTick() {
  if (_tmReadAt && millis() - _tmReadAt < 1000) return;
  _tmReadAt = millis();
  hwRtcRead(&_tm);
}

void statusDraw(Arduino_GFX& g) {
  g.fillScreen(PAL.bg);
  g.setTextSize(1);
  int y = SAFE_T + 4;
  auto ln = [&](uint16_t col, const char* fmt, ...) {
    char b[40]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
    g.setTextColor(col, PAL.bg);
    g.setCursor(SAFE_L, y); g.print(b); y += 10;
  };

  // Title
  g.setTextSize(2);
  g.setTextColor(PAL.body, PAL.bg);
  g.setCursor(SAFE_L, y); g.print("fleet glance");
  g.setTextSize(1);
  y += 24;

  // Clock — only once a bridge/hub time sync has set the RTC.
  if (dataRtcValid()) {
    char hm[8]; snprintf(hm, sizeof(hm), "%02u:%02u", _tm.H, _tm.M);
    uiCenteredText(g, hm, W / 2, y + 16, 4, PAL.text, PAL.bg);
    g.setTextSize(1);
    y += 40;
  } else {
    y += 6;
  }

  // Agent
  ln(PAL.text, "agent");
  if (tama.connected) {
    ln(PAL.textDim, "  %u running  %u waiting",
       tama.sessionsRunning, tama.sessionsWaiting);
    uint32_t tok = tama.tokensToday;
    if (tok >= 1000) ln(PAL.textDim, "  tokens today %lu.%luk",
                        (unsigned long)(tok / 1000), (unsigned long)((tok % 1000) / 100));
    else             ln(PAL.textDim, "  tokens today %lu", (unsigned long)tok);
  } else {
    ln(PAL.textDim, "  no agent data");
  }
  y += 6;

  // Wi-Fi
  WifiLinkState ws = wifiLinkState();
  ln(PAL.text, "wifi");
  ln(ws == WIFI_LINK_ONLINE ? PAL.body : PAL.textDim, "  %s  %s",
     (const char*[]){"off","portal","joining","online","failed"}[ws],
     wifiLinkSsid());
  if (wifiLinkIp()[0]) ln(PAL.textDim, "  %s", wifiLinkIp());
  y += 6;

  // Hub: url tail (the host:port end is the identifying part) + health.
  ln(PAL.text, "hub");
  if (httpTransportConfigured()) {
    const char* hu = httpTransportUrl();
    size_t hl = strlen(hu);
    ln(PAL.textDim, "  %s", hl > 24 ? hu + (hl - 24) : hu);
    bool ok = httpTransportHealthy();
    ln(ok ? PAL.body : 0xF800, "  %s", ok ? "ok" : "down");
  } else {
    ln(PAL.textDim, "  none");
  }

  // Footer: heap + fw version, pinned to the bottom of the safe region.
#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "dev"
#endif
  g.setTextColor(PAL.textDim, PAL.bg);
  g.setCursor(SAFE_L, SAFE_B - 10);
  g.printf("%uKB  %s", ESP.getFreeHeap() / 1024, BUDDY_FW_VERSION);
}
