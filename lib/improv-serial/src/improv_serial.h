#pragma once
#include <stdint.h>
#include <stddef.h>

// Improv Serial v1 (https://www.improv-wifi.com/serial/) on a port that also
// carries line-JSON. A packet is "IMPROV" + version 1 + type + len + data +
// checksum (sum of all prior bytes, mod 256). Detection only starts at the
// beginning of a line, so 'I' inside a JSON line is never taken.

enum ImprovType : uint8_t {
  IMPROV_TYPE_STATE  = 0x01,
  IMPROV_TYPE_ERROR  = 0x02,
  IMPROV_TYPE_RPC    = 0x03,
  IMPROV_TYPE_RESULT = 0x04,
};

enum ImprovCmd : uint8_t {
  IMPROV_CMD_WIFI_SETTINGS = 0x01,
  IMPROV_CMD_GET_STATE     = 0x02,
  IMPROV_CMD_GET_INFO      = 0x03,
};

enum ImprovState : uint8_t {
  IMPROV_STATE_READY        = 0x02,
  IMPROV_STATE_PROVISIONING = 0x03,
  IMPROV_STATE_PROVISIONED  = 0x04,
};

enum ImprovError : uint8_t {
  IMPROV_ERR_NONE           = 0x00,
  IMPROV_ERR_INVALID_RPC    = 0x01,
  IMPROV_ERR_UNKNOWN_RPC    = 0x02,
  IMPROV_ERR_UNABLE_CONNECT = 0x03,
  IMPROV_ERR_UNKNOWN        = 0xFF,
};

enum ImprovFeed : uint8_t {
  IMPROV_NOT_MINE,      // byte belongs to the line framer
  IMPROV_MORE,          // consumed; packet in progress
  IMPROV_PACKET,        // consumed; a full packet is in the parser
  IMPROV_BAD_CHECKSUM,  // consumed; packet complete but corrupt
  IMPROV_DROPPED,       // consumed; not an Improv packet after all
};

const uint32_t IMPROV_STALL_MS = 200;

struct ImprovParser {
  uint8_t  buf[9 + 255 + 1];
  uint16_t len;
  uint32_t lastMs;
  bool     done;   // buf holds a finished packet until the next byte
};

/** improvFeed runs one byte through the parser; atLineStart = the line framer is empty. */
ImprovFeed improvFeed(ImprovParser& p, uint8_t b, bool atLineStart, uint32_t nowMs);

/** improvPacketType / improvPacketData read the packet after IMPROV_PACKET. */
uint8_t improvPacketType(const ImprovParser& p);
const uint8_t* improvPacketData(const ImprovParser& p, uint8_t* len);

struct ImprovRpc {
  uint8_t cmd;
  char ssid[33];
  char pass[65];
};

/** improvParseRpc decodes an RPC packet's data; false if malformed. */
bool improvParseRpc(const uint8_t* data, uint8_t len, ImprovRpc& out);

/** improvEncodeState / improvEncodeError build a framed packet (+ '\n'); return its length. */
size_t improvEncodeState(uint8_t* out, size_t cap, ImprovState s);
size_t improvEncodeError(uint8_t* out, size_t cap, ImprovError e);

/** improvEncodeResult frames an RPC result carrying n strings (n may be 0). */
size_t improvEncodeResult(uint8_t* out, size_t cap, uint8_t cmd,
                          const char* const* strings, uint8_t n);
