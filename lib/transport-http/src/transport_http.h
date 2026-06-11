#pragma once
#include "line_bus.h"

// HTTP polling as a ByteSource — the same newline-delimited JSON the BLE
// and USB pipes carry, but fetched from a hub server over Wi-Fi (rides on
// wifi-link being ONLINE). A background task polls GET <base>/poll for
// pending lines and flushes device replies to POST <base>/push, so the
// blocking HTTP round-trips never stall the UI loop.
//
// Wire contract with the hub:
//   GET  /poll  → 200, body = zero or more \n-terminated JSON lines
//   POST /push  ← body = \n-terminated JSON lines from the device
void httpTransportStart(const char* baseUrl);  // idempotent; restarts on new url
void httpTransportStop();
void httpTransportSetToken(const char* token);                 // Bearer auth (hub fleet key)
void httpTransportSetIdentity(const char* id, const char* model, const char* fw);
bool httpTransportConfigured();
bool httpTransportHealthy();   // last poll round-trip succeeded
const char* httpTransportUrl();

ByteSource& httpByteSource();
