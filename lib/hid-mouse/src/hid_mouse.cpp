#include "hid_mouse.h"

#ifdef BUDDY_BLE_NIMBLE
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Arduino.h>
#include "ble_bridge.h"

static NimBLEHIDDevice*     _hid = nullptr;
static NimBLECharacteristic* _input = nullptr;
static bool _enabled = false;
static volatile bool _subscribed = false;

// NimBLE 2.x has no subscriber-count getter — track it via the callback.
class InputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    _subscribed = (subValue & 0x0001) != 0;   // bit 0 = notifications
    Serial.printf("[hid] host %s\n", _subscribed ? "subscribed" : "unsubscribed");
  }
};

// Boot-protocol mouse: report id 1, 3 buttons + X/Y/wheel (relative).
static const uint8_t MOUSE_REPORT_MAP[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Buttons)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x03,        //     Usage Maximum (3)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x03,        //     Report Count (3)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data, Variable, Absolute)
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x05,        //     Report Size (5)
  0x81, 0x03,        //     Input (Constant) — padding
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data, Variable, Relative)
  0xC0,              //   End Collection
  0xC0               // End Collection
};

// Rebuild the advertisement: flags + name always; HID service UUID + mouse
// appearance only while enabled. The NUS 128-bit UUID stays in the scan
// response (the 31-byte legacy adv can't fit everything).
static void _applyAdvertising() {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();

  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setName(bleDeviceName());
  if (_enabled) {
    advData.setAppearance(0x03C2);                     // generic mouse
    advData.addServiceUUID(NimBLEUUID((uint16_t)0x1812));  // HID service
  }
  adv->setAdvertisementData(advData);
  adv->start();
}

void hidMouseInit() {
  NimBLEServer* server = NimBLEDevice::getServer();
  if (!server || _hid) return;
  _hid = new NimBLEHIDDevice(server);
  _hid->setManufacturer("buddy-kit");
  _hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
  _hid->setHidInfo(0x00, 0x01);
  _hid->setReportMap((uint8_t*)MOUSE_REPORT_MAP, sizeof(MOUSE_REPORT_MAP));
  _input = _hid->getInputReport(1);
  _input->setCallbacks(new InputCallbacks());
  _hid->startServices();
  _hid->setBatteryLevel(100);
  Serial.println("[hid] mouse service ready");
}

void hidMouseSetEnabled(bool on) {
  if (!_hid || on == _enabled) return;
  _enabled = on;
  _applyAdvertising();
  Serial.printf("[hid] mouse %s\n", on ? "advertising" : "hidden");
}

bool hidMouseEnabled() { return _enabled; }

bool hidMouseConnected() {
  // _subscribed can linger if a host drops without unsubscribing; AND with
  // a live connection so a stale flag can't claim "connected" forever.
  NimBLEServer* s = NimBLEDevice::getServer();
  return _input && _subscribed && s && s->getConnectedCount() > 0;
}

void hidMouseMove(int8_t dx, int8_t dy, int8_t wheel) {
  if (!hidMouseConnected()) return;
  uint8_t report[4] = { 0, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
  _input->setValue(report, sizeof(report));
  _input->notify();
}

#else  // Bluedroid builds: safe no-ops

void hidMouseInit() {}
void hidMouseSetEnabled(bool) {}
bool hidMouseEnabled() { return false; }
bool hidMouseConnected() { return false; }
void hidMouseMove(int8_t, int8_t, int8_t) {}

#endif
