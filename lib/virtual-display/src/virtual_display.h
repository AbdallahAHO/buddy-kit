#pragma once
#include <stdint.h>
#include "line_bus.h"

// Framebuffer tee: streams the logical canvas over a byte pipe as JSON
// lines, so a browser (or any client) can show the real pixels the real
// render code produced. Not a renderer and not a scene format — a tap on
// the one buffer every screen already draws into. The same tee is how a
// screenless board gets a "display": same firmware, frames go here only.
//
// Wire shape (one JSON object per line, same framing as everything else):
//   host → device  {"cmd":"vdp","on":true}     start streaming (keyframe first)
//                  {"cmd":"vdp","on":false}    stop
//                  {"cmd":"vdp","full":true}   force a full keyframe
//   device → host  {"vdp":"info","w":184,"h":224,"sh":2}      on start
//                  {"vdp":"s","y":12,"b":"<base64 RGB565-LE, sh rows>"}
//
// Heapless: a fixed stripe-hash table + one static line buffer. Only
// changed stripes ship, a few per tick, from a rotating cursor — a busy
// region can't starve the rest of the frame, and the loop never blocks
// on a full frame. Frames may tear across stripes (the canvas keeps
// changing mid-scan); for a preview that's the right trade.

// fb points at w*h RGB565 pixels (the Arduino_Canvas framebuffer). out is
// whatever pipe(s) the app wants frames on — typically USB only: stripe
// lines run ~1 KB, which BLE NUS would fragment and the hub poll would
// flood. Canvas wider than 256 px is not supported (line buffer bound).
void virtualDisplayInit(const uint16_t* fb, int w, int h, LineOut* out);

// Apply a {"cmd":"vdp",...} doc. Returns false if unsupported (canvas too
// wide / not initialised). The app acks on its own bus.
bool virtualDisplayCmd(JsonDocument& doc);

// Call once per loop, after render. No-op while off.
void virtualDisplayTick();

bool virtualDisplayActive();
