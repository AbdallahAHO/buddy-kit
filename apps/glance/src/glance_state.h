#pragma once
#include <stdint.h>
#include "hw/display.h"
#include "agent_state.h"

// The glance store: one shared agent snapshot, read by the status screen.
// Definition lives in main.cpp. Screen-local state (RTC cache) stays inside
// screens/status.cpp. Same pattern as buddy's app_state.h, minus UI modes —
// glance has exactly one surface and no input routing.

// Keep the upstream name everywhere in the app.
using TamaState = AgentState;

// Logical canvas geometry (board-derived compile-time constants).
constexpr int W = HW_W;
constexpr int H = HW_H;

// Live agent session state (filled by dataPoll from USB + hub HTTP).
extern TamaState tama;

// glance_link.cpp — transport pump + liveness + RTC time-sync
void dataPoll(TamaState* out);
bool dataConnected();
bool dataRtcValid();

// glance_commands.cpp — command dispatch + ack fan-out + transport wiring
bool glanceCommand(JsonDocument& doc);
void glanceCommandsInit();

// screens/status.cpp — the one full-screen panel
void statusTick();                   // 1 Hz RTC cache refresh
void statusDraw(Arduino_GFX& g);
