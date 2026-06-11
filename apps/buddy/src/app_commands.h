#pragma once
#include <ArduinoJson.h>
#include "line_bus.h"

// Buddy-app command dispatch + the shared reply fan-out (USB + BLE + HTTP).
extern LineOut gLineOut;

// Route an incoming {"cmd":...} doc. Returns true if consumed (caller skips
// state-update parsing). Preserves the original xfer.h quirk: with no
// transfer active, unknown cmds are swallowed EXCEPT "permission".
bool appCommand(JsonDocument& doc);
void appCommandsInit();
