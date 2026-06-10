#pragma once
#include <stdint.h>
#include <ArduinoJson.h>

// The Claude-session state schema — the contract of the whole system.
// Sources fill it (agentApplyJson), UI reads it, selectors derive from it.
// Pure: no Arduino.h, no clocks, no persistence — native-testable.
struct AgentState {
  uint8_t  sessionsTotal;
  uint8_t  sessionsRunning;
  uint8_t  sessionsWaiting;
  bool     recentlyCompleted;
  uint32_t tokensToday;
  uint32_t lastUpdated;
  char     msg[24];
  bool     connected;
  char     lines[8][92];
  uint8_t  nLines;
  uint16_t lineGen;          // bumps when lines change — lets UI reset scroll
  char     promptId[40];     // pending permission request ID; empty = no prompt
  char     promptTool[20];
  char     promptHint[44];
};

// The 7-state persona contract every face renders.
enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };

// Session counts → persona. Pure selector.
PersonaState agentDerive(const AgentState& s);

// What a snapshot apply produced, for the app to act on (write RTC, feed
// token stats). Keeps hardware and persistence out of this lib.
struct AgentEvent {
  enum Kind : uint8_t { NONE, STATE_UPDATED, TIME_SYNC } kind = NONE;
  bool     hasTokens = false;
  uint32_t tokens = 0;        // bridge-cumulative output tokens
  uint32_t epoch = 0;         // valid when TIME_SYNC: epoch seconds (UTC)
  int32_t  tzOffsetSec = 0;   // valid when TIME_SYNC
};

// Apply one pre-parsed JSON doc to the state. The caller must have routed
// {"cmd":...} docs to its dispatcher first. nowMs is passed in (was
// millis()) so the lib stays clock-free.
AgentEvent agentApplyJson(JsonDocument& doc, AgentState& s, uint32_t nowMs);

// Link-time injection: the app returns esp_random(); native tests return a
// seeded PRNG. Used by the matrixify ASCII filter.
extern "C" uint32_t agentStateRandom();
