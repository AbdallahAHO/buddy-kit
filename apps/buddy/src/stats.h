#pragma once
#include <stdint.h>
#include <stddef.h>

// Persistent pet stats + user settings, backed by NVS (namespace "buddy").
// Load once at boot; save sparingly (NVS sectors have ~100K write cycles) —
// on significant events only, never on a timer.

constexpr uint32_t TOKENS_PER_LEVEL = 50000;

struct Stats {
  uint32_t napSeconds;       // cumulative face-down time
  uint16_t approvals;
  uint16_t denials;
  uint16_t velocity[8];      // ring buffer: seconds-to-respond per approval
  uint8_t  velIdx;
  uint8_t  velCount;
  uint8_t  level;
  uint32_t tokens;           // cumulative output tokens, drives level
};

struct Settings {
  bool sound;
  bool bt;
  bool wifi;     // gates Wi-Fi auto-connect on boot
  bool led;
  bool hud;
  bool jiggler;  // BLE HID mouse jiggler mode
  uint8_t clockRot;  // 0=auto 1=portrait 2=landscape
};

void statsLoad();
void statsSave();
void statsOnApproval(uint32_t secondsToRespond);
void statsOnBridgeTokens(uint32_t bridgeTotal);  // cumulative; delta-synced with first-sight latch
bool statsPollLevelUp();
void statsOnDenial();
void statsMarkDirty();
void statsOnNapEnd(uint32_t seconds);
void statsOnWake();
uint16_t statsMedianVelocity();   // median of the ring buffer, 0 if empty
uint8_t  statsMoodTier();         // 0..4: velocity base, denial ratio drags down
uint8_t  statsEnergyTier();       // 0..5: tops up on nap end, drains 1 per 2h
uint8_t  statsFedProgress();      // 0..10 pips within the current level
void statsFactoryWipe();          // clear the whole NVS namespace

void settingsLoad();
void settingsSave();
Settings& settings();
const Stats& stats();

void petNameLoad();
void petNameSet(const char* name);
const char* petName();
void ownerSet(const char* name);
const char* ownerName();
uint8_t speciesIdxLoad();
void speciesIdxSave(uint8_t idx);
