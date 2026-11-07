#include "notify_pair.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <ArduinoJson.h>

static const uint32_t DEFAULT_TTL_S      = 600;
static const uint32_t DEFAULT_INTERVAL_S = 5;

static bool _copy(char* dst, size_t cap, const char* src) {
  if (!src || strlen(src) >= cap) return false;
  strcpy(dst, src);
  return true;
}

static void _fail(PairSession& s, PairPhase phase, const char* why) {
  s.phase = phase;
  snprintf(s.error, sizeof(s.error), "%s", why);
  memset(s.secret, 0, sizeof(s.secret));
}

bool pairBaseUrl(const char* wsUrl, char* base, size_t cap) {
  const char* scheme;
  const char* rest;
  if (!strncmp(wsUrl, "wss://", 6))     { scheme = "https://"; rest = wsUrl + 6; }
  else if (!strncmp(wsUrl, "ws://", 5)) { scheme = "http://";  rest = wsUrl + 5; }
  else return false;
  size_t hostLen = strcspn(rest, "/?#");
  if (hostLen == 0 || strlen(scheme) + hostLen >= cap) return false;
  snprintf(base, cap, "%s%.*s", scheme, (int)hostLen, rest);
  return true;
}

void pairHostOf(const char* url, char* out, size_t cap) {
  const char* p = strstr(url, "://");
  p = p ? p + 3 : url;
  size_t n = strcspn(p, ":/?#");
  if (n >= cap) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

void pairFormatCode(const char* in, char* out, size_t cap) {
  char raw[16];
  size_t n = 0;
  for (; *in && n < sizeof(raw) - 1; in++)
    if (isalnum((unsigned char)*in)) raw[n++] = (char)toupper((unsigned char)*in);
  raw[n] = 0;
  if (n == 6) snprintf(out, cap, "%.3s-%.3s", raw, raw + 3);
  else        snprintf(out, cap, "%s", raw);
}

bool pairStart(PairSession& s, const char* wsUrl, const char* console) {
  memset(&s, 0, sizeof(s));
  if (!wsUrl || !pairBaseUrl(wsUrl, s.base, sizeof(s.base)) ||
      !_copy(s.connect, sizeof(s.connect), wsUrl)) {
    _fail(s, PAIR_FAILED, "bad url");
    return false;
  }
  pairHostOf(console && console[0] ? console : s.base, s.shownHost, sizeof(s.shownHost));
  s.phase = PAIR_REQUESTING;
  return true;
}

void pairOnCreated(PairSession& s, int status, const char* body, uint32_t nowMs) {
  if (s.phase != PAIR_REQUESTING) return;
  if (status <= 0)  { _fail(s, PAIR_FAILED, "no connection"); return; }
  if (status == 429) { _fail(s, PAIR_FAILED, "rate limited"); return; }
  if (status != 201 && status != 200) {
    char why[24]; snprintf(why, sizeof(why), "http %d", status);
    _fail(s, PAIR_FAILED, why);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body ? body : "") ||
      !_copy(s.pairingId, sizeof(s.pairingId), doc["pairingId"]) ||
      !_copy(s.secret, sizeof(s.secret), doc["deviceSecret"]) ||
      !doc["code"].is<const char*>() || !s.pairingId[0] || !s.secret[0]) {
    _fail(s, PAIR_FAILED, "bad reply");
    return;
  }
  pairFormatCode(doc["code"], s.code, sizeof(s.code));
  uint32_t ttl = doc["expiresIn"] | DEFAULT_TTL_S;
  uint32_t interval = doc["interval"] | DEFAULT_INTERVAL_S;
  if (ttl == 0 || ttl > 3600) ttl = DEFAULT_TTL_S;
  if (interval < 1) interval = 1;
  if (interval > 60) interval = 60;
  s.ttlMs = ttl * 1000u;
  s.intervalMs = interval * 1000u;
  s.waitStartMs = nowMs;
  s.nextPollMs = nowMs + s.intervalMs;
  s.phase = PAIR_WAITING;
}

void pairOnPoll(PairSession& s, int status, const char* body, uint32_t nowMs) {
  if (s.phase != PAIR_WAITING) return;
  s.nextPollMs = nowMs + s.intervalMs;
  if (status == 404 || status == 410) { _fail(s, PAIR_EXPIRED, "expired"); return; }
  if (status != 200) {
    // No response, 429 or 5xx: keep polling; the deadline bounds it.
    if (status > 0 && status < 500 && status != 429) _fail(s, PAIR_FAILED, "rejected");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body ? body : "")) return;
  const char* st = doc["status"] | "";
  if (!strcmp(st, "expired")) { _fail(s, PAIR_EXPIRED, "expired"); return; }
  if (strcmp(st, "approved") != 0) return;

  const char* token = doc["token"];
  if (!token || !token[0] || !_copy(s.token, sizeof(s.token), token)) {
    _fail(s, PAIR_FAILED, "bad token");
    return;
  }
  const char* connect = doc["connect"];
  if (connect && (!strncmp(connect, "ws://", 5) || !strncmp(connect, "wss://", 6)))
    _copy(s.connect, sizeof(s.connect), connect);
  memset(s.secret, 0, sizeof(s.secret));
  s.phase = PAIR_APPROVED;
}

bool pairPollDue(const PairSession& s, uint32_t nowMs) {
  return s.phase == PAIR_WAITING && (int32_t)(nowMs - s.nextPollMs) >= 0;
}

bool pairCheckDeadline(PairSession& s, uint32_t nowMs) {
  if (s.phase != PAIR_WAITING) return false;
  if (nowMs - s.waitStartMs < s.ttlMs + s.intervalMs) return false;
  _fail(s, PAIR_EXPIRED, "expired");
  return true;
}

uint32_t pairSecondsLeft(const PairSession& s, uint32_t nowMs) {
  if (s.phase != PAIR_WAITING) return 0;
  uint32_t gone = nowMs - s.waitStartMs;
  return gone >= s.ttlMs ? 0 : (s.ttlMs - gone + 999) / 1000;
}

void pairWipe(PairSession& s) { memset(&s, 0, sizeof(s)); }
