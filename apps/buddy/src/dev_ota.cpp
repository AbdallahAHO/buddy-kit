#include "dev_ota.h"
#include <ArduinoOTA.h>
#include "wifi_link.h"
#include "ble_bridge.h"

static bool _begun = false;

void devOtaTick() {
  // Start once, the first time we're online; ArduinoOTA needs the STA up.
  if (!_begun) {
    if (wifiLinkState() != WIFI_LINK_ONLINE) return;
    ArduinoOTA.setHostname(bleDeviceName());   // buddy device name, e.g. Claude-D41A
    ArduinoOTA.begin();
    _begun = true;
    Serial.printf("[devota] ArduinoOTA on %s.local\n", bleDeviceName());
  }
  ArduinoOTA.handle();
}
