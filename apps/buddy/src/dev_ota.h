#pragma once
// Dev convenience: ArduinoOTA push over the LAN, so you can
//   pio run -e ... -t upload --upload-port buddy-XXXX.local
// instead of unplugging. Advertises mDNS once Wi-Fi is up. This is the
// developer inner loop; product updates go through lib/ota (HTTP pull).
void devOtaTick();
