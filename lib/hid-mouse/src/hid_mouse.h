#pragma once
#include <stdint.h>

// BLE HID mouse (HID-over-GATT) attached to the SAME GATT server as the NUS
// bridge — one device identity, one bond store, the same passkey pairing
// flow. Enabling adds the HID service UUID + mouse appearance to the
// advertisement so hosts (macOS Bluetooth settings) list the device as an
// input device; disabling removes them again.
//
// NimBLE stacks only (-DBUDDY_BLE_NIMBLE). On Bluedroid builds every call
// is a safe no-op so app code needs no #ifdefs.
void hidMouseInit();                 // once, after bleInit()
void hidMouseSetEnabled(bool on);    // toggles advertising identity
bool hidMouseEnabled();
bool hidMouseConnected();            // a host subscribed to the input report
void hidMouseMove(int8_t dx, int8_t dy, int8_t wheel = 0);
