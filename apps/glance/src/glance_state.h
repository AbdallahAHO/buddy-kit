#pragma once
#include <stdint.h>
#include "hw/display.h"
#include "agent_state.h"
#include "line_bus.h"
#include "notify_queue.h"

// The glance store: the agent snapshot (status screen) and the notify inbox
// (notify screen). Definitions live in main.cpp. Screen-local state (RTC
// cache, LED pulse) stays inside screens/*.cpp. Same pattern as buddy's
// app_state.h: the notify screen covers the status panel while anything is
// pending; the BOOT button is routed in input_router.cpp.

// Keep the upstream name everywhere in the app.
using TamaState = AgentState;

// Logical canvas geometry (board-derived compile-time constants).
constexpr int W = HW_W;
constexpr int H = HW_H;

// Live agent session state (filled by dataPoll from USB + hub + socket).
extern TamaState tama;

// Pending notify asks (filled by the notify cmd, drained by input_router).
extern NotifyQueue gNotify;

// Ack fan-out (USB + hub + notify socket), glance_commands.cpp.
extern LineOut gLineOut;

// glance_link.cpp — transport pump + liveness + RTC time-sync
void dataPoll(TamaState* out);
bool dataConnected();
bool dataRtcValid();

// glance_commands.cpp — command dispatch + ack fan-out + transport wiring
bool glanceCommand(JsonDocument& doc);
void glanceCommandsInit();

// screens/status.cpp — the default full-screen panel
void statusTick();                   // 1 Hz RTC cache refresh
void statusDraw(Arduino_GFX& g);

// screens/notify.cpp — covers the status panel while gNotify is non-empty
void notifyScreenTick();             // ttl expiry, seen ack, LED pulse
void notifyScreenDraw(Arduino_GFX& g);

// input_router.cpp — button A → notify actions
void inputRoute();
