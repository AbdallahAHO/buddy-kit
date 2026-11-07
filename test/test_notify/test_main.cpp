// Native tests for lib/notify-queue: the notify v1 inbox state machine.
#include <unity.h>
#include <string.h>
#include "notify_queue.h"

static NotifyQueue q;

void setUp() { notifyInit(q); }
void tearDown() {}

static NotifyOffer offer(const char* json, uint32_t nowMs = 0) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return notifyOffer(q, doc, nowMs);
}

static const char* ASK =
  "{\"cmd\":\"notify\",\"v\":1,\"id\":\"ask_1\",\"kind\":\"question\","
  "\"title\":\"Deploy?\",\"body\":\"3 commits behind\",\"actions\":[\"Yes\",\"No\"],\"ttl\":300}";

void test_question_is_queued_with_actions() {
  TEST_ASSERT_EQUAL_INT(NOTIFY_QUEUED, offer(ASK));
  const NotifyItem* it = notifyHead(q);
  TEST_ASSERT_NOT_NULL(it);
  TEST_ASSERT_EQUAL_STRING("ask_1", it->id);
  TEST_ASSERT_EQUAL_STRING("Deploy?", it->title);
  TEST_ASSERT_EQUAL_UINT8(2, it->nActions);
  TEST_ASSERT_EQUAL_STRING("No", it->actions[1]);
  TEST_ASSERT_EQUAL_UINT32(300000, it->ttlMs);
}

void test_same_id_twice_is_duplicate_not_second_item() {
  offer(ASK);
  TEST_ASSERT_EQUAL_INT(NOTIFY_DUPLICATE, offer(ASK));
  TEST_ASSERT_EQUAL_UINT8(1, q.n);
}

void test_approval_without_actions_gets_approve_deny() {
  offer("{\"cmd\":\"notify\",\"id\":\"a\",\"kind\":\"approval\",\"title\":\"t\"}");
  TEST_ASSERT_EQUAL_UINT8(2, notifyHead(q)->nActions);
  TEST_ASSERT_EQUAL_STRING("Approve", notifyHead(q)->actions[0]);
  TEST_ASSERT_EQUAL_STRING("Deny", notifyHead(q)->actions[1]);
}

void test_message_ignores_actions_and_dismisses() {
  offer("{\"cmd\":\"notify\",\"id\":\"m\",\"kind\":\"message\",\"actions\":[\"x\"]}");
  TEST_ASSERT_EQUAL_UINT8(0, notifyHead(q)->nActions);
  char line[128];
  notifyChoose(q, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("{\"ack\":\"notify\",\"id\":\"m\",\"state\":\"dismissed\"}\n", line);
  TEST_ASSERT_NULL(notifyHead(q));
}

void test_fifth_item_is_refused_without_ack() {
  char json[96];
  for (int i = 0; i < NOTIFY_CAP; i++) {
    snprintf(json, sizeof(json), "{\"cmd\":\"notify\",\"id\":\"n%d\",\"kind\":\"message\"}", i);
    TEST_ASSERT_EQUAL_INT(NOTIFY_QUEUED, offer(json));
  }
  TEST_ASSERT_EQUAL_INT(NOTIFY_FULL, offer("{\"cmd\":\"notify\",\"id\":\"n9\",\"kind\":\"message\"}"));
  TEST_ASSERT_EQUAL_UINT8(NOTIFY_CAP, q.n);
}

void test_ttl_expiry_is_measured_from_receipt() {
  offer(ASK, 1000);                       // ttl 300 s
  TEST_ASSERT_EQUAL_UINT8(0, notifyExpire(q, 1000 + 299999));
  TEST_ASSERT_EQUAL_UINT8(1, notifyExpire(q, 1000 + 300000));
  TEST_ASSERT_NULL(notifyHead(q));
}

void test_ttl_survives_millis_wrap() {
  offer(ASK, 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_UINT8(0, notifyExpire(q, 0x00001000u));
}

void test_zero_ttl_is_dropped_on_arrival() {
  TEST_ASSERT_EQUAL_INT(NOTIFY_EXPIRED,
    offer("{\"cmd\":\"notify\",\"id\":\"x\",\"kind\":\"message\",\"ttl\":0}"));
  TEST_ASSERT_EQUAL_UINT8(0, q.n);
}

void test_missing_ttl_never_expires() {
  offer("{\"cmd\":\"notify\",\"id\":\"x\",\"kind\":\"message\"}");
  TEST_ASSERT_EQUAL_UINT8(0, notifyExpire(q, 0xFFFFFFF0u));
}

void test_retract_removes_head_and_resets_selection() {
  offer(ASK);
  offer("{\"cmd\":\"notify\",\"id\":\"ask_2\",\"kind\":\"question\",\"actions\":[\"A\",\"B\",\"C\"]}");
  notifyNextAction(q);
  TEST_ASSERT_EQUAL_INT(NOTIFY_RETRACTED,
    offer("{\"cmd\":\"notify\",\"v\":1,\"id\":\"ask_1\",\"retract\":true,\"reason\":\"answered\"}"));
  TEST_ASSERT_EQUAL_STRING("ask_2", notifyHead(q)->id);
  TEST_ASSERT_EQUAL_UINT8(0, q.sel);
}

void test_retract_unknown_id_is_ignored() {
  TEST_ASSERT_EQUAL_INT(NOTIFY_IGNORED,
    offer("{\"cmd\":\"notify\",\"id\":\"nope\",\"retract\":true}"));
}

void test_seen_fires_once_per_head() {
  offer(ASK);
  TEST_ASSERT_TRUE(notifyMarkSeen(q));
  TEST_ASSERT_FALSE(notifyMarkSeen(q));
}

void test_next_action_wraps_and_choose_answers_label() {
  offer(ASK);
  notifyNextAction(q);
  notifyNextAction(q);                    // back to "Yes"
  notifyNextAction(q);                    // "No"
  char line[128];
  size_t n = notifyChoose(q, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING(
    "{\"ack\":\"notify\",\"id\":\"ask_1\",\"state\":\"answered\",\"answer\":\"No\"}\n", line);
  TEST_ASSERT_EQUAL_size_t(strlen(line), n);
  TEST_ASSERT_NULL(notifyHead(q));
}

void test_overlong_id_is_invalid() {
  TEST_ASSERT_EQUAL_INT(NOTIFY_INVALID, offer(
    "{\"cmd\":\"notify\",\"id\":\"0123456789012345678901234567890123456789X\",\"kind\":\"message\"}"));
}

void test_fields_truncate_to_protocol_limits() {
  offer("{\"cmd\":\"notify\",\"id\":\"t\",\"kind\":\"question\","
        "\"title\":\"0123456789012345678901234567890123456789EXTRA\","
        "\"actions\":[\"abcdefghijklMORE\",\"2\",\"3\",\"4\",\"5\"]}");
  const NotifyItem* it = notifyHead(q);
  TEST_ASSERT_EQUAL_size_t(NOTIFY_TITLE_MAX, strlen(it->title));
  TEST_ASSERT_EQUAL_STRING("abcdefghijkl", it->actions[0]);
  TEST_ASSERT_EQUAL_UINT8(NOTIFY_ACTIONS_MAX, it->nActions);
}

void test_truncation_never_splits_utf8() {
  // 39 ASCII bytes + "é" (2 bytes) → the é would straddle byte 40.
  offer("{\"cmd\":\"notify\",\"id\":\"u\",\"kind\":\"message\","
        "\"title\":\"012345678901234567890123456789012345678\xc3\xa9\"}");
  TEST_ASSERT_EQUAL_size_t(39, strlen(notifyHead(q)->title));
}

void test_ack_line_escapes_quotes() {
  char line[96];
  notifyAckLine(line, sizeof(line), "i\"d", "answered", "a\\b");
  TEST_ASSERT_EQUAL_STRING(
    "{\"ack\":\"notify\",\"id\":\"i\\\"d\",\"state\":\"answered\",\"answer\":\"a\\\\b\"}\n", line);
}

void test_ack_line_too_small_returns_zero() {
  char line[16];
  TEST_ASSERT_EQUAL_size_t(0, notifyAckLine(line, sizeof(line), "ask_1", "seen"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_question_is_queued_with_actions);
  RUN_TEST(test_same_id_twice_is_duplicate_not_second_item);
  RUN_TEST(test_approval_without_actions_gets_approve_deny);
  RUN_TEST(test_message_ignores_actions_and_dismisses);
  RUN_TEST(test_fifth_item_is_refused_without_ack);
  RUN_TEST(test_ttl_expiry_is_measured_from_receipt);
  RUN_TEST(test_ttl_survives_millis_wrap);
  RUN_TEST(test_zero_ttl_is_dropped_on_arrival);
  RUN_TEST(test_missing_ttl_never_expires);
  RUN_TEST(test_retract_removes_head_and_resets_selection);
  RUN_TEST(test_retract_unknown_id_is_ignored);
  RUN_TEST(test_seen_fires_once_per_head);
  RUN_TEST(test_next_action_wraps_and_choose_answers_label);
  RUN_TEST(test_overlong_id_is_invalid);
  RUN_TEST(test_fields_truncate_to_protocol_limits);
  RUN_TEST(test_truncation_never_splits_utf8);
  RUN_TEST(test_ack_line_escapes_quotes);
  RUN_TEST(test_ack_line_too_small_returns_zero);
  return UNITY_END();
}
