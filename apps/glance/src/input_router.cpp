// The one place glance reads input. Ladder, topmost first:
//   1. pairing overlay → hold = cancel; once finished, any press closes it
//   2. notify ask      → short = next action, hold ≥ 600 ms = choose/dismiss
//   3. nothing focused → ignored
// Never paints.
#include <Arduino.h>
#include "glance_state.h"
#include "hw/hw.h"

static const uint32_t LONG_PRESS_MS = 600;

// Returns true when the pairing overlay took the press.
static bool _routePairing(HwBtn& a, const PairView& pair) {
  static bool owned = false;
  if (a.wasPressed) owned = pair.phase != PAIR_IDLE;
  if (!owned) return false;
  if (pair.phase == PAIR_IDLE) { owned = false; return true; }
  bool finished = pair.phase == PAIR_EXPIRED || pair.phase == PAIR_FAILED;
  if ((finished && a.wasReleased) || (!finished && a.pressedFor(LONG_PRESS_MS))) {
    pairLinkClose();
    owned = false;
  }
  if (a.wasReleased) owned = false;
  return true;
}

void inputRoute(const PairView& pair) {
  HwBtn& a = hwBtnA();
  if (_routePairing(a, pair)) return;
  // A press acts only on the ask that was on screen when it began: one that
  // arrives, or replaces a retracted head, mid-press is never answered unseen.
  static char pressedId[NOTIFY_ID_MAX + 1] = "";
  static bool longFired = false;
  const NotifyItem* head = notifyHead(gNotify);
  if (a.wasPressed) {
    strcpy(pressedId, head ? head->id : "");
    longFired = false;
  }
  if (!pressedId[0]) return;
  if (!head || strcmp(head->id, pressedId) != 0) { pressedId[0] = 0; return; }

  if (a.pressedFor(LONG_PRESS_MS) && !longFired) {
    longFired = true;
    char line[128];
    size_t n = notifyChoose(gNotify, line, sizeof(line));
    if (n) gLineOut.write((const uint8_t*)line, n);
    pressedId[0] = 0;
  }
  if (a.wasReleased) {
    if (!longFired) notifyNextAction(gNotify);
    pressedId[0] = 0;
  }
}
