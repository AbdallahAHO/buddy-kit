#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

// The device side of notify v1 (docs/notify-protocol.md): a fixed inbox of
// pending asks. Pure state machine — time comes in as nowMs, acks come out
// as values the app writes through gLineOut. No Arduino, no heap.

const uint8_t NOTIFY_CAP         = 4;
const uint8_t NOTIFY_ID_MAX      = 40;
const uint8_t NOTIFY_TITLE_MAX   = 40;
const uint8_t NOTIFY_BODY_MAX    = 160;
const uint8_t NOTIFY_ACTIONS_MAX = 4;
const uint8_t NOTIFY_ACTION_MAX  = 12;

enum NotifyKind : uint8_t { NK_MESSAGE, NK_NOTIFICATION, NK_APPROVAL, NK_QUESTION };

struct NotifyItem {
  char id[NOTIFY_ID_MAX + 1];
  char title[NOTIFY_TITLE_MAX + 1];
  char body[NOTIFY_BODY_MAX + 1];
  char actions[NOTIFY_ACTIONS_MAX][NOTIFY_ACTION_MAX + 1];
  uint8_t nActions;
  NotifyKind kind;
  uint32_t receivedMs;
  uint32_t ttlMs;     // 0 = no expiry
  bool seen;
};

// items[0] is the head (the one on screen); sel indexes its actions.
struct NotifyQueue {
  NotifyItem items[NOTIFY_CAP];
  uint8_t n;
  uint8_t sel;
};

enum NotifyOffer : uint8_t {
  NOTIFY_QUEUED,     // new item → ack delivered
  NOTIFY_DUPLICATE,  // id already queued → ack delivered again
  NOTIFY_RETRACTED,  // retract removed a queued item → no ack
  NOTIFY_IGNORED,    // retract for an unknown id → no ack
  NOTIFY_FULL,       // inbox full → no ack; the server keeps it queued
  NOTIFY_EXPIRED,    // ttl <= 0 on arrival → no ack
  NOTIFY_INVALID,    // missing/overlong id → no ack
};

/** notifyInit empties the queue. */
void notifyInit(NotifyQueue& q);

/** notifyOffer applies one server frame ({"cmd":"notify",…}) received at nowMs. */
NotifyOffer notifyOffer(NotifyQueue& q, JsonDocument& doc, uint32_t nowMs);

/** notifyExpire drops items whose ttl has passed; returns how many. */
uint8_t notifyExpire(NotifyQueue& q, uint32_t nowMs);

/** notifyHead is the item on screen, or nullptr when the inbox is empty. */
const NotifyItem* notifyHead(const NotifyQueue& q);

/** notifyMarkSeen flags the head as shown; true only the first time. */
bool notifyMarkSeen(NotifyQueue& q);

/** notifyNextAction moves the highlight to the head's next action (wraps). */
void notifyNextAction(NotifyQueue& q);

/**
 * notifyChoose resolves the head: answered with the highlighted label, or
 * dismissed when it has no actions. Writes the ack line (with '\n') into
 * buf, pops the head, and returns the line length (0 if empty).
 */
size_t notifyChoose(NotifyQueue& q, char* buf, size_t cap);

/** notifyAckLine formats {"ack":"notify","id",…,"state",…[,"answer"]}\n; returns its length. */
size_t notifyAckLine(char* buf, size_t cap, const char* id, const char* state,
                     const char* answer = nullptr);
