#pragma once
#include "line_bus.h"

// A held WebSocket as a ByteSource (notify v1, docs/notify-protocol.md).
// Each received text frame is fed to the framer with a '\n' appended; each
// '\n'-terminated line written goes out as one text frame without it.
// A background task owns the socket: it connects only while wifi-link is
// ONLINE, sends "ping" every 30 s, reconnects when nothing arrives for 75 s,
// backs off 1 s → 60 s (±20 %, reset after 60 s up), and stops for good on
// close code 4001 until the next wsTransportStart().
//
// ws:// is plain TCP (LAN dev). wss:// verifies the server against the
// IDF certificate bundle; there is no unverified mode.
void wsTransportStart(const char* url, const char* token);  // restarts on new config
void wsTransportStop();
bool wsTransportConfigured();
bool wsTransportConnected();
bool wsTransportRevoked();    // server closed with 4001
const char* wsTransportUrl();

ByteSource& wsByteSource();
