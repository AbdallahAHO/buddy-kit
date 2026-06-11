#pragma once
#include <stdint.h>
#include "hw/display.h"
#include "agent_link.h"   // TamaState + PersonaState

// The app store: shared state every module reads through this one header.
// Definitions live in main.cpp. Screen-local state (page indexes, scroll,
// RTC cache) lives inside each screens/*.cpp instead.

// Logical canvas geometry (board-derived compile-time constants).
constexpr int W = HW_W;
constexpr int H = HW_H;
constexpr int CX = W / 2;
constexpr int CY_BASE = 120;

enum DisplayMode { DISP_NORMAL, DISP_PET, DISP_INFO, DISP_COUNT };

// Live agent session state (filled by dataPoll from all transports).
extern TamaState tama;

// Persona
extern PersonaState baseState, activeState;
extern const char* stateNames[];

// UI mode + overlays
extern uint8_t displayMode;
extern bool menuOpen, settingsOpen, resetOpen, wifiSetupOpen;
extern uint8_t brightLevel;
extern bool screenOff, napping;

// Approval round-trip
extern bool responseSent;
extern uint32_t promptArrivedMs;

// Rendering flags
extern bool buddyMode;        // ASCII species vs GIF character
extern bool gifAvailable;     // a character set is installed on LittleFS

// App actions
void wake();                  // screen wake + interaction stamp (main.cpp)

// Misc shared
extern char btName[16];       // advertised BLE name, shown on the info page
extern uint32_t _clkLastRead; // clock RTC cache stamp; zeroed on time-sync
