#pragma once
#include <stdint.h>

// Mouse-jiggler mode: while enabled and an HID host is connected, nudge the
// pointer ±1 px every 30 s (net-zero — invisible, but resets the host's
// idle timers). The BLE HID mouse itself is lib/hid-mouse; this is just
// the policy + settings glue.
void jigglerApply(bool on);       // reflect settings().jiggler into hid-mouse
void jigglerTick(uint32_t now);
