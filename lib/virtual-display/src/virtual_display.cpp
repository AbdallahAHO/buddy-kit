#include "virtual_display.h"
#include <string.h>
#include <stdio.h>

static const int STRIPE_H    = 2;
static const int MAX_STRIPES = 128;                       // canvas ≤ 256 rows
static const int MAX_W       = 256;                       // line buffer bound
static const int MAX_STRIPE_BYTES = MAX_W * STRIPE_H * 2;

static const uint16_t* _fb = nullptr;
static LineOut* _out = nullptr;
static int  _w = 0, _h = 0, _stripes = 0;
static bool _on = false, _infoPending = false;
static int  _scan = 0;                  // rotating stripe cursor
static uint32_t _hash[MAX_STRIPES];     // 0 = never sent → dirty

static uint32_t fnv1a(const uint8_t* d, size_t n) {
  uint32_t h = 2166136261u;
  while (n--) { h ^= *d++; h *= 16777619u; }
  return h;
}

static size_t b64enc(const uint8_t* in, size_t n, char* out) {
  static const char T[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0, i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
    out[o++] = T[(v >> 18) & 63]; out[o++] = T[(v >> 12) & 63];
    out[o++] = T[(v >>  6) & 63]; out[o++] = T[v & 63];
  }
  if (i < n) {
    uint32_t v = ((uint32_t)in[i] << 16) | (i + 1 < n ? (uint32_t)in[i+1] << 8 : 0);
    out[o++] = T[(v >> 18) & 63]; out[o++] = T[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    out[o++] = '=';
  }
  return o;
}

void virtualDisplayInit(const uint16_t* fb, int w, int h, LineOut* out) {
  _fb = fb; _w = w; _h = h; _out = out;
  _stripes = (h + STRIPE_H - 1) / STRIPE_H;
  if (_stripes > MAX_STRIPES) _stripes = MAX_STRIPES;
}

bool virtualDisplayActive() { return _on; }

bool virtualDisplayCmd(JsonDocument& doc) {
  if (!_fb || !_out || _w > MAX_W) return false;
  if (doc["on"].is<bool>()) _on = doc["on"].as<bool>();
  if ((doc["on"] | false) || (doc["full"] | false)) {
    memset(_hash, 0, sizeof(_hash));    // everything dirty → keyframe
    _scan = 0;
    _infoPending = true;
  }
  return true;
}

void virtualDisplayTick() {
  if (!_on || !_fb || !_out) return;
  if (_infoPending) {
    char ib[64];
    int n = snprintf(ib, sizeof(ib), "{\"vdp\":\"info\",\"w\":%d,\"h\":%d,\"sh\":%d}\n",
                     _w, _h, STRIPE_H);
    _out->write((const uint8_t*)ib, (size_t)n);
    _infoPending = false;
  }
  // Budget per tick: scan a window for changes, ship at most a few stripes
  // (~1 KB each) so a full keyframe spreads over ~40 ticks instead of
  // stalling one loop iteration on ~115 KB of serial writes.
  static char line[32 + ((MAX_STRIPE_BYTES + 2) / 3) * 4 + 8];
  const int SCAN_MAX = 28, SEND_MAX = 3;
  int scanned = 0, sent = 0;
  while (scanned < SCAN_MAX && sent < SEND_MAX) {
    int s = _scan;
    _scan = (_scan + 1) % _stripes;
    scanned++;
    int y = s * STRIPE_H;
    int rows = (y + STRIPE_H <= _h) ? STRIPE_H : _h - y;
    const uint8_t* px = (const uint8_t*)(_fb + (size_t)y * _w);
    size_t nbytes = (size_t)_w * rows * 2;
    uint32_t hv = fnv1a(px, nbytes);
    if (hv == _hash[s]) continue;
    _hash[s] = hv;
    int o = snprintf(line, sizeof(line), "{\"vdp\":\"s\",\"y\":%d,\"b\":\"", y);
    o += (int)b64enc(px, nbytes, line + o);
    line[o++] = '"'; line[o++] = '}'; line[o++] = '\n';
    _out->write((const uint8_t*)line, (size_t)o);
    sent++;
  }
}
