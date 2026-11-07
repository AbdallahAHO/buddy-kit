#pragma once
#include <stdint.h>
#include "hw/display.h"
#include "agent_state.h"
#include "line_bus.h"
#include "notify_queue.h"
#include "notify_pair.h"
#include "improv_serial.h"

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
void notifyScreenTick(bool visible); // ttl expiry; seen ack only when visible
void notifyScreenDraw(Arduino_GFX& g);

// pair_link.cpp — notify pairing (HTTP task); the loop sees a copy
struct PairView {
  PairPhase phase;
  bool waitingForWifi;
  uint32_t secondsLeft;
  char code[10];
  char host[64];
  char error[32];
};
bool pairLinkStart(const char* wsUrl, const char* console);  // false on a bad url
void pairLinkClose();                // cancel, or dismiss a finished pairing
PairView pairLinkView();
void pairLinkTick();                 // approved → notifyLinkAdopt, then close

// screens/pair.cpp — topmost overlay while a pairing is active or just ended
void pairScreenDraw(Arduino_GFX& g, const PairView& v);

// glance_commands.cpp — persist url + token and dial (pairing's hand-off)
void notifyLinkAdopt(const char* url, const char* token);

// improv_link.cpp — Improv Serial on USB
void improvHandlePacket(const ImprovParser& p, bool checksumOk);
void improvTick();                   // reports the outcome of a Wi-Fi join

// status_led.cpp — the one LED owner
void ledTick(const PairView& pair);

// input_router.cpp — button A → pairing overlay, then notify actions
void inputRoute(const PairView& pair);
