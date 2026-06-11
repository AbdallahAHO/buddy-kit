#pragma once
// Glue between the transports and the agent-state lib: demo mode, liveness
// tracking, RTC time-sync, token→stats feed. State lives in agent_link.cpp.
#include "agent_state.h"

// Keep the upstream name everywhere in the app.
using TamaState = AgentState;

void dataPoll(TamaState* out);
void dataSetDemo(bool on);
bool dataDemo();
bool dataConnected();
bool dataBtActive();
const char* dataScenarioName();
bool dataRtcValid();
