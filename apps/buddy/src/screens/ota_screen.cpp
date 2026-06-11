#include "screens/ota_screen.h"   // pulls in the lib ota.h (OtaProgress, otaState)
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"

static bool _active = false;

bool otaScreenActive() { return _active; }

void otaScreenOnProgress(const OtaProgress& p) {
  _active = (p.phase != OTA_IDLE);
}

void otaScreenDraw(Arduino_GFX& g) {
  const OtaProgress& p = otaState();
  uint16_t bg = BLACK, fg = WHITE, dim = 0x8410;
  g.fillScreen(bg);
  g.setTextSize(1);
  uiCenteredText(g, "FIRMWARE UPDATE", CX, 40, 1, dim, bg);

  if (p.phase == OTA_FAILED) {
    uiCenteredText(g, "FAILED", CX, 100, 2, HOT, bg);
    uiCenteredText(g, p.error, CX, 130, 1, dim, bg);
    uiCenteredText(g, "any key to dismiss", CX, H - 20, 1, dim, bg);
    return;
  }

  const char* label = p.phase == OTA_APPLYING ? "applying..." : "downloading";
  uiCenteredText(g, label, CX, 90, 1, fg, bg);

  int barW = W - 40, barX = 20, barY = 110;
  g.drawRect(barX, barY, barW, 12, dim);
  int fill = (int)((long)(barW - 2) * p.percent / 100);
  if (fill > 0) g.fillRect(barX + 1, barY + 1, fill, 10, GREEN);

  char pct[8]; snprintf(pct, sizeof(pct), "%u%%", p.percent);
  uiCenteredText(g, pct, CX, 140, 2, fg, bg);
  uiCenteredText(g, "do not unplug", CX, H - 20, 1, dim, bg);
}
