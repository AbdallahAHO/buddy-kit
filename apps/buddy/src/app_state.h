#pragma once
#include <stdint.h>

// Cross-file app globals — the store. Definitions live in main.cpp; this
// header is the one place other app TUs reach for shared state.
extern bool buddyMode;        // ASCII species vs GIF character rendering
extern bool gifAvailable;     // a character set is installed on LittleFS
extern bool wifiSetupOpen;    // QR pairing screen is showing
extern uint32_t _clkLastRead; // clock-screen RTC cache; zero forces re-read
