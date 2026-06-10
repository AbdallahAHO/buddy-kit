#pragma once
#include <stdint.h>
#include <ArduinoJson.h>
#include "line_bus.h"

// The desktop folder-push protocol (char_begin → file → chunk → file_end →
// … → char_end), base64 chunks acked one-by-one — LittleFS writes block on
// flash erase and the UART RX buffer is tiny, so the per-chunk ack IS the
// flow control. Storage policy (where files land, wipe rules, fit checks,
// what happens on completion) is injected via FileSink by the app.
struct FileSink {
  // Fit-check + wipe + prepare for a new set. On failure fill availOut
  // (free bytes, reported as the ack's n) and err text.
  bool (*begin)(void* u, const char* setName, uint32_t totalBytes,
                uint32_t* availOut, char* err, size_t errLen);
  bool (*open) (void* u, const char* relPath);     // open file for write
  bool (*write)(void* u, const uint8_t* d, size_t n);
  void (*close)(void* u);
  bool (*finish)(void* u, const char* setName);    // char_end: activate the set
  void* user;
};

void filePushInit(const FileSink* sink, LineOut* out);

// Handle one parsed command doc. char_begin is accepted any time; the other
// four only while a transfer is active. Returns true iff consumed (acks are
// written to the LineOut). Caller owns the "swallow unknown cmds" policy.
bool filePushHandle(JsonDocument& doc);

bool     filePushActive();
uint32_t filePushProgress();
uint32_t filePushTotal();
