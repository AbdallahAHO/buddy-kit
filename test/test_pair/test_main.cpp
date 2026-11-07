// Native tests for lib/notify-pair: the device-code pairing state machine.
#include <unity.h>
#include <string.h>
#include "notify_pair.h"

static PairSession s;
static const char* WSS = "wss://notify.aaho.cloud/api/devices/connect";
static const char* CREATED =
  "{\"pairingId\":\"prq_1\",\"deviceSecret\":\"prs_x\",\"code\":\"k7m4qx\",\"expiresIn\":600,\"interval\":5}";

void setUp() { pairWipe(s); }
void tearDown() {}

static void startWaiting(uint32_t now = 1000) {
  pairStart(s, WSS, nullptr);
  pairOnCreated(s, 201, CREATED, now);
}

void test_base_url_maps_wss_to_https_and_drops_path() {
  char b[64];
  TEST_ASSERT_TRUE(pairBaseUrl(WSS, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("https://notify.aaho.cloud", b);
}

void test_base_url_keeps_port_for_ws() {
  char b[64];
  TEST_ASSERT_TRUE(pairBaseUrl("ws://192.168.1.5:8787/api/devices/connect", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.5:8787", b);
}

void test_base_url_rejects_http() {
  char b[64];
  TEST_ASSERT_FALSE(pairBaseUrl("https://x", b, sizeof(b)));
}

void test_code_formats_as_xxx_dash_xxx() {
  char c[10];
  pairFormatCode("k7m4qx", c, sizeof(c));
  TEST_ASSERT_EQUAL_STRING("K7M-4QX", c);
  pairFormatCode("K7M-4QX", c, sizeof(c));
  TEST_ASSERT_EQUAL_STRING("K7M-4QX", c);
}

void test_shown_host_prefers_console() {
  pairStart(s, WSS, "https://console.aaho.cloud/pair");
  TEST_ASSERT_EQUAL_STRING("console.aaho.cloud", s.shownHost);
  pairStart(s, WSS, nullptr);
  TEST_ASSERT_EQUAL_STRING("notify.aaho.cloud", s.shownHost);
}

void test_created_moves_to_waiting_with_code_and_schedule() {
  startWaiting(1000);
  TEST_ASSERT_EQUAL_INT(PAIR_WAITING, s.phase);
  TEST_ASSERT_EQUAL_STRING("K7M-4QX", s.code);
  TEST_ASSERT_FALSE(pairPollDue(s, 5999));
  TEST_ASSERT_TRUE(pairPollDue(s, 6000));
  TEST_ASSERT_EQUAL_UINT32(600, pairSecondsLeft(s, 1000));
}

void test_rate_limited_create_fails() {
  pairStart(s, WSS, nullptr);
  pairOnCreated(s, 429, "", 0);
  TEST_ASSERT_EQUAL_INT(PAIR_FAILED, s.phase);
  TEST_ASSERT_EQUAL_STRING("rate limited", s.error);
}

void test_created_without_secret_fails() {
  pairStart(s, WSS, nullptr);
  pairOnCreated(s, 201, "{\"pairingId\":\"prq_1\",\"code\":\"ABCDEF\"}", 0);
  TEST_ASSERT_EQUAL_INT(PAIR_FAILED, s.phase);
}

void test_pending_reschedules() {
  startWaiting(0);
  pairOnPoll(s, 200, "{\"status\":\"pending\"}", 5000);
  TEST_ASSERT_EQUAL_INT(PAIR_WAITING, s.phase);
  TEST_ASSERT_FALSE(pairPollDue(s, 9999));
  TEST_ASSERT_TRUE(pairPollDue(s, 10000));
}

void test_approved_keeps_token_and_connect_and_wipes_secret() {
  startWaiting(0);
  pairOnPoll(s, 200,
    "{\"status\":\"approved\",\"token\":\"dtk_abc\",\"destinationId\":\"dst_1\","
    "\"connect\":\"wss://other.host/api/devices/connect\"}", 5000);
  TEST_ASSERT_EQUAL_INT(PAIR_APPROVED, s.phase);
  TEST_ASSERT_EQUAL_STRING("dtk_abc", s.token);
  TEST_ASSERT_EQUAL_STRING("wss://other.host/api/devices/connect", s.connect);
  TEST_ASSERT_EQUAL_STRING("", s.secret);
}

void test_approved_without_connect_falls_back_to_configured_url() {
  startWaiting(0);
  pairOnPoll(s, 200, "{\"status\":\"approved\",\"token\":\"dtk_abc\"}", 5000);
  TEST_ASSERT_EQUAL_STRING(WSS, s.connect);
}

void test_expired_status_404_and_410_all_expire() {
  const int codes[] = { 404, 410 };
  for (int c : codes) {
    startWaiting(0);
    pairOnPoll(s, c, "", 5000);
    TEST_ASSERT_EQUAL_INT(PAIR_EXPIRED, s.phase);
  }
  startWaiting(0);
  pairOnPoll(s, 200, "{\"status\":\"expired\"}", 5000);
  TEST_ASSERT_EQUAL_INT(PAIR_EXPIRED, s.phase);
}

void test_network_error_and_5xx_keep_polling() {
  startWaiting(0);
  pairOnPoll(s, -1, nullptr, 5000);
  pairOnPoll(s, 503, "", 10000);
  pairOnPoll(s, 429, "", 15000);
  TEST_ASSERT_EQUAL_INT(PAIR_WAITING, s.phase);
}

void test_local_deadline_expires_one_interval_after_ttl() {
  startWaiting(0);
  TEST_ASSERT_FALSE(pairCheckDeadline(s, 600000 + 4999));
  TEST_ASSERT_TRUE(pairCheckDeadline(s, 600000 + 5000));
  TEST_ASSERT_EQUAL_INT(PAIR_EXPIRED, s.phase);
}

void test_poll_ignored_when_not_waiting() {
  pairStart(s, WSS, nullptr);
  pairOnPoll(s, 200, "{\"status\":\"approved\",\"token\":\"dtk_abc\"}", 0);
  TEST_ASSERT_EQUAL_INT(PAIR_REQUESTING, s.phase);
}

void test_interval_is_clamped() {
  pairStart(s, WSS, nullptr);
  pairOnCreated(s, 201,
    "{\"pairingId\":\"p\",\"deviceSecret\":\"s\",\"code\":\"ABCDEF\",\"interval\":0}", 0);
  TEST_ASSERT_EQUAL_UINT32(1000, s.intervalMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_base_url_maps_wss_to_https_and_drops_path);
  RUN_TEST(test_base_url_keeps_port_for_ws);
  RUN_TEST(test_base_url_rejects_http);
  RUN_TEST(test_code_formats_as_xxx_dash_xxx);
  RUN_TEST(test_shown_host_prefers_console);
  RUN_TEST(test_created_moves_to_waiting_with_code_and_schedule);
  RUN_TEST(test_rate_limited_create_fails);
  RUN_TEST(test_created_without_secret_fails);
  RUN_TEST(test_pending_reschedules);
  RUN_TEST(test_approved_keeps_token_and_connect_and_wipes_secret);
  RUN_TEST(test_approved_without_connect_falls_back_to_configured_url);
  RUN_TEST(test_expired_status_404_and_410_all_expire);
  RUN_TEST(test_network_error_and_5xx_keep_polling);
  RUN_TEST(test_local_deadline_expires_one_interval_after_ttl);
  RUN_TEST(test_poll_ignored_when_not_waiting);
  RUN_TEST(test_interval_is_clamped);
  return UNITY_END();
}
