#include "notify_queue.h"
#include <string.h>
#include <stdio.h>

// Copies at most max bytes, backing off so a cut never splits a UTF-8 char.
static void _copyTrunc(char* dst, const char* src, size_t max) {
  size_t n = strlen(src);
  if (n > max) {
    n = max;
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

static NotifyKind _kind(const char* k) {
  if (!k) return NK_MESSAGE;
  if (!strcmp(k, "notification")) return NK_NOTIFICATION;
  if (!strcmp(k, "approval"))     return NK_APPROVAL;
  if (!strcmp(k, "question"))     return NK_QUESTION;
  return NK_MESSAGE;
}

static int _find(const NotifyQueue& q, const char* id) {
  for (uint8_t i = 0; i < q.n; i++)
    if (!strcmp(q.items[i].id, id)) return i;
  return -1;
}

static void _remove(NotifyQueue& q, uint8_t i) {
  if (i >= q.n) return;
  memmove(&q.items[i], &q.items[i + 1], (q.n - i - 1) * sizeof(NotifyItem));
  q.n--;
  if (i == 0) q.sel = 0;
}

static bool _expired(const NotifyItem& it, uint32_t nowMs) {
  return it.ttlMs && (uint32_t)(nowMs - it.receivedMs) >= it.ttlMs;
}

void notifyInit(NotifyQueue& q) { memset(&q, 0, sizeof(q)); }

NotifyOffer notifyOffer(NotifyQueue& q, JsonDocument& doc, uint32_t nowMs) {
  const char* id = doc["id"];
  if (!id || !id[0] || strlen(id) > NOTIFY_ID_MAX) return NOTIFY_INVALID;

  if (doc["retract"] | false) {
    int i = _find(q, id);
    if (i < 0) return NOTIFY_IGNORED;
    _remove(q, (uint8_t)i);
    return NOTIFY_RETRACTED;
  }

  if (_find(q, id) >= 0) return NOTIFY_DUPLICATE;

  uint32_t ttlMs = 0;
  if (!doc["ttl"].isNull()) {
    long ttl = doc["ttl"] | 0L;
    if (ttl <= 0) return NOTIFY_EXPIRED;
    if (ttl > 4000000L) ttl = 4000000L;   // keeps ttl*1000 inside uint32
    ttlMs = (uint32_t)ttl * 1000u;
  }
  if (q.n >= NOTIFY_CAP) return NOTIFY_FULL;

  NotifyItem& it = q.items[q.n];
  memset(&it, 0, sizeof(it));
  _copyTrunc(it.id, id, NOTIFY_ID_MAX);
  _copyTrunc(it.title, doc["title"] | "", NOTIFY_TITLE_MAX);
  _copyTrunc(it.body, doc["body"] | "", NOTIFY_BODY_MAX);
  it.kind = _kind(doc["kind"]);
  it.receivedMs = nowMs;
  it.ttlMs = ttlMs;

  // message/notification carry no actions even if the frame lists some.
  if (it.kind == NK_APPROVAL || it.kind == NK_QUESTION) {
    for (JsonVariant a : doc["actions"].as<JsonArray>()) {
      const char* s = a.as<const char*>();
      if (!s || !s[0]) continue;
      if (it.nActions >= NOTIFY_ACTIONS_MAX) break;
      _copyTrunc(it.actions[it.nActions++], s, NOTIFY_ACTION_MAX);
    }
    if (it.kind == NK_APPROVAL && it.nActions == 0) {
      strcpy(it.actions[0], "Approve");
      strcpy(it.actions[1], "Deny");
      it.nActions = 2;
    }
  }
  q.n++;
  return NOTIFY_QUEUED;
}

uint8_t notifyExpire(NotifyQueue& q, uint32_t nowMs) {
  uint8_t dropped = 0;
  for (uint8_t i = 0; i < q.n;) {
    if (_expired(q.items[i], nowMs)) { _remove(q, i); dropped++; }
    else i++;
  }
  return dropped;
}

const NotifyItem* notifyHead(const NotifyQueue& q) {
  return q.n ? &q.items[0] : nullptr;
}

bool notifyMarkSeen(NotifyQueue& q) {
  if (!q.n || q.items[0].seen) return false;
  q.items[0].seen = true;
  return true;
}

void notifyNextAction(NotifyQueue& q) {
  if (!q.n || q.items[0].nActions == 0) return;
  q.sel = (uint8_t)((q.sel + 1) % q.items[0].nActions);
}

size_t notifyChoose(NotifyQueue& q, char* buf, size_t cap) {
  if (!q.n) return 0;
  const NotifyItem& it = q.items[0];
  size_t len = it.nActions
    ? notifyAckLine(buf, cap, it.id, "answered", it.actions[q.sel < it.nActions ? q.sel : 0])
    : notifyAckLine(buf, cap, it.id, "dismissed");
  _remove(q, 0);
  return len;
}

// Appends s as a JSON string body (no quotes); false when it doesn't fit.
static bool _putEscaped(char* buf, size_t cap, size_t& at, const char* s) {
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    char esc[7];
    size_t n;
    if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; n = 2; }
    else if (c < 0x20) n = (size_t)snprintf(esc, sizeof(esc), "\\u%04x", c);
    else { esc[0] = (char)c; n = 1; }
    if (at + n >= cap) return false;
    memcpy(buf + at, esc, n);
    at += n;
  }
  return true;
}

static bool _putRaw(char* buf, size_t cap, size_t& at, const char* s) {
  size_t n = strlen(s);
  if (at + n >= cap) return false;
  memcpy(buf + at, s, n);
  at += n;
  return true;
}

size_t notifyAckLine(char* buf, size_t cap, const char* id, const char* state,
                     const char* answer) {
  size_t at = 0;
  bool ok = _putRaw(buf, cap, at, "{\"ack\":\"notify\",\"id\":\"")
         && _putEscaped(buf, cap, at, id)
         && _putRaw(buf, cap, at, "\",\"state\":\"")
         && _putEscaped(buf, cap, at, state)
         && _putRaw(buf, cap, at, "\"");
  if (ok && answer)
    ok = _putRaw(buf, cap, at, ",\"answer\":\"")
      && _putEscaped(buf, cap, at, answer)
      && _putRaw(buf, cap, at, "\"");
  ok = ok && _putRaw(buf, cap, at, "}\n");
  if (!ok) { if (cap) buf[0] = 0; return 0; }
  buf[at] = 0;
  return at;
}
