#pragma once
#include <stdint.h>
#include <stddef.h>

// Wi-Fi connectivity + provisioning lifecycle (NOT a ByteSource transport —
// transports like HTTP ride on top of this). Two provisioning channels:
// the SoftAP captive portal (phone scans a WIFI: QR, enters creds in the
// portal page) and direct wifiLinkJoin() (driven by a line-bus command).
//
// All state transitions happen inside wifiLinkTick() — call it every loop.
enum WifiLinkState : uint8_t {
  WIFI_LINK_OFF,       // radio idle, no attempt in progress
  WIFI_LINK_PORTAL,    // SoftAP + captive portal up, waiting for creds
  WIFI_LINK_JOINING,   // STA connecting (portal may still be up behind it)
  WIFI_LINK_ONLINE,    // connected, IP assigned
  WIFI_LINK_FAILED,    // last join failed — wifiLinkError() says why
};

void wifiLinkInit();      // load stored creds; does not touch the radio
void wifiLinkTick();      // pump portal servers + connection state machine

bool wifiLinkHasCreds();
bool wifiLinkConnect();   // async join with stored creds; false if none
void wifiLinkJoin(const char* ssid, const char* pass);  // join now; persists on success
void wifiLinkDisconnect();                              // radio off, state → OFF
void wifiLinkForget();                                  // clear creds + disconnect

void wifiLinkStartPortal();
void wifiLinkStopPortal();    // keeps the STA link if one is up

WifiLinkState wifiLinkState();
const char* wifiLinkSsid();   // target/current network ("" if none)
const char* wifiLinkIp();     // dotted quad when ONLINE, else ""
const char* wifiLinkError();  // last failure reason ("" if none)

// SoftAP identity (valid while the portal is up). The QR text is the
// standard WIFI: payload phone cameras auto-join from.
const char* wifiLinkApSsid();
const char* wifiLinkApPass();
void wifiLinkQrText(char* buf, size_t cap);

// Injected persistence — the app owns the NVS namespace (like faces_store).
bool wifiCredsLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap);
void wifiCredsSave(const char* ssid, const char* pass);
void wifiCredsClear();
