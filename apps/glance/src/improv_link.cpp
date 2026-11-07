// Improv Serial on the USB port (ESP Web Tools "Configure Wi-Fi", notify
// console): get-state, device info, and Wi-Fi settings joined through
// wifi-link (persisted on success). Replies are USB-only binary packets, not
// gLineOut lines: Improv is a USB protocol and its frames aren't JSON.
#include <Arduino.h>
#include "improv_serial.h"
#include "transport_usb.h"
#include "wifi_link.h"
#include "hw/hw.h"
#include "glance_state.h"

static bool _provisioning = false;

static void _send(const uint8_t* b, size_t n) {
  if (n) usbSerialSource().write(nullptr, b, n);
}
static void _state(ImprovState s) { uint8_t b[16]; _send(b, improvEncodeState(b, sizeof(b), s)); }
static void _error(ImprovError e) { uint8_t b[16]; _send(b, improvEncodeError(b, sizeof(b), e)); }
static void _result(uint8_t cmd, const char* const* strs, uint8_t n) {
  uint8_t b[160];
  _send(b, improvEncodeResult(b, sizeof(b), cmd, strs, n));
}

void improvHandlePacket(const ImprovParser& p, bool checksumOk) {
  if (!checksumOk) { _error(IMPROV_ERR_INVALID_RPC); return; }
  if (improvPacketType(p) != IMPROV_TYPE_RPC) return;
  uint8_t len;
  const uint8_t* data = improvPacketData(p, &len);
  ImprovRpc rpc;
  if (!improvParseRpc(data, len, rpc)) { _error(IMPROV_ERR_INVALID_RPC); return; }

  switch (rpc.cmd) {
    case IMPROV_CMD_WIFI_SETTINGS:
      wifiLinkJoin(rpc.ssid, rpc.pass);
      memset(&rpc, 0, sizeof(rpc));
      _provisioning = true;
      _state(IMPROV_STATE_PROVISIONING);
      break;
    case IMPROV_CMD_GET_STATE:
      if (wifiLinkState() == WIFI_LINK_ONLINE && !_provisioning) {
        _state(IMPROV_STATE_PROVISIONED);
        _result(IMPROV_CMD_GET_STATE, nullptr, 0);   // glance serves no device URL
      } else {
        _state(_provisioning ? IMPROV_STATE_PROVISIONING : IMPROV_STATE_READY);
      }
      break;
    case IMPROV_CMD_GET_INFO: {
#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "dev"
#endif
      const char* info[] = { "glance", BUDDY_FW_VERSION, ESP.getChipModel(), BOARD_MODEL_LINE2 };
      _result(IMPROV_CMD_GET_INFO, info, 4);
      break;
    }
    default:
      _error(IMPROV_ERR_UNKNOWN_RPC);
  }
}

void improvTick() {
  if (!_provisioning) return;
  WifiLinkState st = wifiLinkState();
  if (st == WIFI_LINK_ONLINE) {
    _provisioning = false;
    _state(IMPROV_STATE_PROVISIONED);
    _result(IMPROV_CMD_WIFI_SETTINGS, nullptr, 0);
  } else if (st == WIFI_LINK_FAILED || st == WIFI_LINK_OFF) {
    _provisioning = false;
    _error(IMPROV_ERR_UNABLE_CONNECT);
    _state(IMPROV_STATE_READY);
  }
}
