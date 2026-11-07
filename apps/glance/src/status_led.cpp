// The one place glance drives the RGB LED: blue pulse while pairing, amber
// (red for approval) while an ask is pending, green once paired, else off.
// Writes only on change.
#include <Arduino.h>
#include "glance_state.h"
#include "hw/hw.h"

void ledTick(const PairView& pair) {
  uint8_t r = 0, g = 0, b = 0;
  uint32_t ph = millis() % 1600;
  uint32_t tri = ph < 800 ? ph : 1600 - ph;          // 1.6 s triangle, 0..800
  uint8_t k = (uint8_t)((1 + tri * 15 / 800) * 6);   // peak ~96: a WS2812 at 255 glares
  const NotifyItem* ask = notifyHead(gNotify);

  if (pair.phase == PAIR_REQUESTING || pair.phase == PAIR_WAITING) b = k;
  else if (pair.phase == PAIR_APPROVED) g = 64;
  else if (ask && ask->kind == NK_APPROVAL) r = k;
  else if (ask) { r = k; g = (uint8_t)(k * 2 / 5); }

  static uint32_t last = 0;
  uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (rgb == last) return;
  last = rgb;
  hwLed(r, g, b);
}
