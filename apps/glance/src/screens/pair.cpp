// The pairing overlay: the code large, where to enter it, and time left. It
// sits above notify and status and repaints every pixel each frame, so
// closing it is a full clear (ghost rule). Draws only the view it's handed.
#include <Arduino.h>
#include "../glance_state.h"
#include "ui_canvas.h"
#include "hw/hw.h"

static const uint16_t PAIR_BLUE = 0x3D9F;
static const uint16_t DIM       = 0x8410;
static const int HEADER_H       = 18;

static void _center(Arduino_GFX& g, const char* s, int y, int sz, uint16_t fg) {
  uiCenteredText(g, s, W / 2, y, sz, fg, BLACK);
}

void pairScreenDraw(Arduino_GFX& g, const PairView& v) {
  g.fillScreen(BLACK);
  g.fillRect(SAFE_L, SAFE_T, SAFE_W, HEADER_H, PAIR_BLUE);
  g.setTextSize(1);
  g.setTextColor(BLACK, PAIR_BLUE);
  g.setCursor(SAFE_L + 4, SAFE_T + 5);
  g.print("PAIR WITH NOTIFY");

  const char* footer = "hold: cancel";
  int mid = (SAFE_T + HEADER_H + SAFE_B) / 2;

  switch (v.phase) {
    case PAIR_REQUESTING:
      _center(g, v.waitingForWifi ? "waiting for" : "getting a", mid - 10, 2, WHITE);
      _center(g, v.waitingForWifi ? "Wi-Fi" : "code...", mid + 10, 2, WHITE);
      break;

    case PAIR_WAITING: {
      int y = SAFE_T + HEADER_H + 16;
      _center(g, "Enter at", y, 2, DIM);
      y += 20;
      char host[48];
      uint8_t cols = SAFE_W / 12;
      snprintf(host, sizeof(host), "%.*s", cols, v.host);
      _center(g, host, y, (int)strlen(host) * 12 <= SAFE_W ? 2 : 1, WHITE);
      y += 44;
      int sz = SAFE_W / (int)(strlen(v.code) * 6);
      if (sz > 5) sz = 5;
      if (sz < 1) sz = 1;
      _center(g, v.code, y, sz, PAIR_BLUE);
      y += 4 * sz + 24;
      char left[24];
      snprintf(left, sizeof(left), "expires %lu:%02lu",
               (unsigned long)(v.secondsLeft / 60), (unsigned long)(v.secondsLeft % 60));
      _center(g, left, y, 2, DIM);
      break;
    }

    case PAIR_APPROVED:
      _center(g, "Paired", mid - 10, 3, GREEN);
      _center(g, "connecting...", mid + 20, 2, DIM);
      footer = "";
      break;

    case PAIR_EXPIRED:
      _center(g, "pairing", mid - 10, 2, HOT);
      _center(g, "expired", mid + 10, 2, HOT);
      footer = "press: close";
      break;

    default:   // PAIR_FAILED
      _center(g, "pairing failed", mid - 10, 2, HOT);
      _center(g, v.error, mid + 14, 1, DIM);
      footer = "press: close";
      break;
  }

  g.setTextSize(1);
  g.setTextColor(DIM, BLACK);
  g.setCursor(SAFE_L, SAFE_B - 9);
  g.print(footer);
}
