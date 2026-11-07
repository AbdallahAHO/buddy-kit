#include "improv_serial.h"
#include <string.h>

static const char HEADER[] = "IMPROV";
static const uint8_t VERSION = 1;
static const uint8_t HEAD_LEN = 9;   // "IMPROV" + version + type + len

ImprovFeed improvFeed(ImprovParser& p, uint8_t b, bool atLineStart, uint32_t nowMs) {
  if (p.done) { p.done = false; p.len = 0; }
  if (p.len && nowMs - p.lastMs > IMPROV_STALL_MS) p.len = 0;
  if (p.len == 0 && !(atLineStart && b == 'I')) return IMPROV_NOT_MINE;

  p.lastMs = nowMs;
  uint16_t i = p.len;
  p.buf[p.len++] = b;
  if (i < 6) {
    if (b != (uint8_t)HEADER[i]) { p.len = 0; return IMPROV_DROPPED; }
    return IMPROV_MORE;
  }
  if (i == 6 && b != VERSION) { p.len = 0; return IMPROV_DROPPED; }
  if (p.len < HEAD_LEN) return IMPROV_MORE;

  uint16_t total = HEAD_LEN + p.buf[8] + 1;
  if (p.len < total) return IMPROV_MORE;

  uint8_t sum = 0;
  for (uint16_t k = 0; k + 1 < total; k++) sum += p.buf[k];
  p.done = true;
  return sum == p.buf[total - 1] ? IMPROV_PACKET : IMPROV_BAD_CHECKSUM;
}

uint8_t improvPacketType(const ImprovParser& p) { return p.buf[7]; }

const uint8_t* improvPacketData(const ImprovParser& p, uint8_t* len) {
  *len = p.buf[8];
  return p.buf + HEAD_LEN;
}

// Reads one length-prefixed string at data[*at]; false if it overruns.
static bool _takeString(const uint8_t* data, uint8_t len, uint8_t* at, char* out, size_t cap) {
  if (*at >= len) return false;
  uint8_t n = data[(*at)++];
  if (n >= cap || *at + n > len) return false;
  memcpy(out, data + *at, n);
  out[n] = 0;
  *at += n;
  return true;
}

bool improvParseRpc(const uint8_t* data, uint8_t len, ImprovRpc& out) {
  memset(&out, 0, sizeof(out));
  if (len < 2 || data[1] != len - 2) return false;
  out.cmd = data[0];
  if (out.cmd != IMPROV_CMD_WIFI_SETTINGS) return true;
  uint8_t at = 2;
  return _takeString(data, len, &at, out.ssid, sizeof(out.ssid)) && out.ssid[0]
      && _takeString(data, len, &at, out.pass, sizeof(out.pass));
}

static size_t _frame(uint8_t* out, size_t cap, uint8_t type, const uint8_t* data, uint8_t n) {
  size_t total = HEAD_LEN + n + 2;   // + checksum + '\n'
  if (total > cap) return 0;
  memcpy(out, HEADER, 6);
  out[6] = VERSION;
  out[7] = type;
  out[8] = n;
  memcpy(out + HEAD_LEN, data, n);
  uint8_t sum = 0;
  for (size_t k = 0; k < HEAD_LEN + n; k++) sum += out[k];
  out[HEAD_LEN + n] = sum;
  out[HEAD_LEN + n + 1] = '\n';
  return total;
}

size_t improvEncodeState(uint8_t* out, size_t cap, ImprovState s) {
  uint8_t d = s;
  return _frame(out, cap, IMPROV_TYPE_STATE, &d, 1);
}

size_t improvEncodeError(uint8_t* out, size_t cap, ImprovError e) {
  uint8_t d = e;
  return _frame(out, cap, IMPROV_TYPE_ERROR, &d, 1);
}

size_t improvEncodeResult(uint8_t* out, size_t cap, uint8_t cmd,
                          const char* const* strings, uint8_t n) {
  uint8_t data[255];
  size_t at = 2;
  for (uint8_t i = 0; i < n; i++) {
    size_t sl = strlen(strings[i]);
    if (sl > 255 || at + 1 + sl > sizeof(data)) return 0;
    data[at++] = (uint8_t)sl;
    memcpy(data + at, strings[i], sl);
    at += sl;
  }
  data[0] = cmd;
  data[1] = (uint8_t)(at - 2);
  return _frame(out, cap, IMPROV_TYPE_RESULT, data, (uint8_t)at);
}
