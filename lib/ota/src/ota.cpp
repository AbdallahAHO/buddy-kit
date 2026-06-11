#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include <string.h>

static OtaProgressCb _cb = nullptr;
static OtaProgress   _state = { OTA_IDLE, 0, "" };

static void _emit(OtaPhase p, uint8_t pct, const char* err) {
  _state.phase = p;
  _state.percent = pct;
  strncpy(_state.error, err ? err : "", sizeof(_state.error)-1);
  _state.error[sizeof(_state.error)-1] = 0;
  if (_cb) _cb(_state);
}

void otaInit(OtaProgressCb cb) { _cb = cb; }

const OtaProgress& otaState() { return _state; }

void otaPull(const char* url) {
  // Guard: OTA needs a second app slot to write into. On a single-slot
  // table esp_ota_get_next_update_partition returns null — fail loudly
  // instead of bricking.
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    _emit(OTA_FAILED, 0, "no OTA partition");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    _emit(OTA_FAILED, 0, "wifi offline");
    return;
  }

  _emit(OTA_DOWNLOADING, 0, nullptr);

  httpUpdate.rebootOnUpdate(true);     // reboot into the new slot on success
  httpUpdate.onProgress([](int done, int total) {
    _emit(OTA_DOWNLOADING, total ? (uint8_t)((uint64_t)done * 100 / total) : 0, nullptr);
  });
  httpUpdate.onEnd([]() { _emit(OTA_APPLYING, 100, nullptr); });

  WiFiClient client;
  t_httpUpdate_return r = httpUpdate.update(client, url);

  // Only reached on failure (success reboots before returning).
  switch (r) {
    case HTTP_UPDATE_FAILED:
      _emit(OTA_FAILED, 0, httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      _emit(OTA_FAILED, 0, "no update at url");
      break;
    default:
      break;   // HTTP_UPDATE_OK reboots; we don't get here
  }
}
