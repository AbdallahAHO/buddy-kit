#pragma once
#include <stdint.h>

// Over-the-air firmware update by HTTP pull: fetch a .bin and self-flash the
// inactive OTA slot, then reboot into it (with rollback on a bad image).
// Requires a dual-OTA partition table (see apps/buddy/ota_8mb.csv). Rides on
// the device already being online (wifi-link); the URL can come from any
// transport (a hub command, the desktop, a dev script).
//
// The pull blocks while it runs — OTA is a take-over operation. Drive a
// progress screen from the OtaProgress callback; the device reboots itself
// on success, so there's no "done" state to render for long.

enum OtaPhase : uint8_t { OTA_IDLE, OTA_DOWNLOADING, OTA_APPLYING, OTA_FAILED };

struct OtaProgress {
  OtaPhase phase;
  uint8_t  percent;     // 0..100 while DOWNLOADING
  char     error[48];   // set when FAILED
};

// Called frequently during a pull so the app can paint a progress bar.
typedef void (*OtaProgressCb)(const OtaProgress&);
void otaInit(OtaProgressCb cb);

// Pull <url> and apply. Returns only on FAILURE (success reboots). Safe to
// call from a command handler. No-op + FAILED if not on a dual-OTA table.
void otaPull(const char* url);

const OtaProgress& otaState();
