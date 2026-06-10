#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ArduinoJson.h>

// The byte-pipe contract every transport satisfies (USB serial, BLE NUS,
// later HTTP/SSE). C-style vtable: no heap, no virtuals, no std::function —
// contracts must hold on a PSRAM-less 512 KB C6.
struct ByteSource {
  size_t (*available)(void* ctx);
  int    (*read)(void* ctx);                               // -1 = nothing
  size_t (*write)(void* ctx, const uint8_t* d, size_t n);  // replies/acks
  void*  ctx;
};

// Accumulates bytes into newline-delimited lines (the wire format is one
// JSON object per line). Same logic as the original _LineBuf, decoupled
// from Stream so any ByteSource can feed it.
template<size_t N>
struct LineFramer {
  char buf[N];
  uint16_t len = 0;
  // Feed one byte; returns the NUL-terminated line on '\n'/'\r' when
  // non-empty, else nullptr. Overlong lines are truncated, not split.
  const char* feed(char c) {
    if (c == '\n' || c == '\r') {
      if (len > 0) { buf[len] = 0; len = 0; return buf; }
      return nullptr;
    }
    if (len < N - 1) buf[len++] = c;
    return nullptr;
  }
};

// Fan-out writer: acks and replies go to every registered transport (the
// desktop may be on either pipe; writes to a clientless pipe just drop).
struct LineOut {
  ByteSource* sinks[4];
  uint8_t n = 0;
  void add(ByteSource* s) { if (n < 4) sinks[n++] = s; }
  void write(const uint8_t* d, size_t len) {
    for (uint8_t i = 0; i < n; i++) sinks[i]->write(sinks[i]->ctx, d, len);
  }
};

// {"ack":"<what>","ok":<ok>,"n":<n>}\n — byte-identical to the original _xAck.
inline void lineBusAck(LineOut& out, const char* what, bool ok, uint32_t n = 0) {
  char b[64];
  int len = snprintf(b, sizeof(b), "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}\n",
                     what, ok ? "true" : "false", (unsigned long)n);
  out.write((const uint8_t*)b, (size_t)len);
}

// Fixed-size command dispatch — a const table, no registration at runtime.
typedef bool (*CmdHandler)(JsonDocument& doc, void* user);
struct CmdEntry { const char* cmd; CmdHandler fn; };

inline bool lineBusDispatch(const CmdEntry* table, uint8_t n,
                            JsonDocument& doc, void* user) {
  const char* cmd = doc["cmd"];
  if (!cmd) return false;
  for (uint8_t i = 0; i < n; i++)
    if (strcmp(table[i].cmd, cmd) == 0) return table[i].fn(doc, user);
  return false;
}
