#include "passkey.h"
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"
#include "character.h"
#include "ble_bridge.h"

void passkeyDraw(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  g.fillScreen(p.bg);
  g.setTextSize(1);
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_L, 56);  g.print("BLUETOOTH PAIRING");
  g.setCursor(SAFE_L, SAFE_B - 32); g.print("enter on desktop:");
  g.setTextSize(3);
  g.setTextColor(p.text, p.bg);
  char b[8]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  g.setCursor((W - 18 * 6) / 2, 110);
  g.print(b);
}

