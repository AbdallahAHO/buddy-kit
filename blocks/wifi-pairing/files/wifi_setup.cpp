#include "wifi_setup.h"
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"
#include "character.h"
#include "buddy.h"
#include "wifi_link.h"

// QR pairing screen: WIFI: QR phones auto-join, AP creds as text fallback,
// live status line. Full-screen like drawPasskey.
void wifiSetupDraw(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  g.fillScreen(p.bg);
  g.setTextSize(1);
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_L, 10); g.print("WIFI SETUP");

  char qrText[64];
  wifiLinkQrText(qrText, sizeof(qrText));
  uiQr(g, qrText, CX, 24, 4);

  char l[40];
  snprintf(l, sizeof(l), "%s / %s", wifiLinkApSsid(), wifiLinkApPass());
  uiCenteredText(g, l, CX, 160, 1, p.textDim, p.bg);

  const char* status = "scan with your phone";
  uint16_t sc = p.text;
  switch (wifiLinkState()) {
    case WIFI_LINK_JOINING:
      snprintf(l, sizeof(l), "joining %s...", wifiLinkSsid()); status = l; break;
    case WIFI_LINK_ONLINE:
      snprintf(l, sizeof(l), "online: %s", wifiLinkIp()); status = l; sc = GREEN; break;
    case WIFI_LINK_FAILED:
      snprintf(l, sizeof(l), "failed: %s", wifiLinkError()); status = l; sc = HOT; break;
    default: break;
  }
  uiCenteredText(g, status, CX, 178, 1, sc, p.bg);
  uiCenteredText(g, "any key: close", CX, SAFE_B - 8, 1, p.textDim, p.bg);
}

void wifiSetupClose() {
  wifiSetupOpen = false;
  wifiLinkStopPortal();
  // The QR screen painted the full canvas — clear it all on the way out.
  applyDisplayMode();
  if (buddyMode) buddyInvalidate();
}

