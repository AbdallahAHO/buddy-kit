#include "jiggler.h"
#include "hid_mouse.h"
#include "stats.h"

static const uint32_t JIGGLE_EVERY_MS = 30000;
static uint32_t _nextMoveMs = 0;

void jigglerApply(bool on) {
  hidMouseSetEnabled(on);
}

void jigglerTick(uint32_t now) {
  if (!settings().jiggler || !hidMouseConnected()) return;
  if ((int32_t)(now - _nextMoveMs) < 0) return;
  _nextMoveMs = now + JIGGLE_EVERY_MS;
  hidMouseMove(1, 0);
  hidMouseMove(-1, 0);   // net-zero nudge — resets idle timers invisibly
}
