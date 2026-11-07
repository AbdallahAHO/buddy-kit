// Native tests for lib/improv-serial: framing next to line-JSON, RPC decode, encoders.
#include <unity.h>
#include <string.h>
#include "improv_serial.h"

static ImprovParser p;

void setUp() { memset(&p, 0, sizeof(p)); }
void tearDown() {}

// Builds a client RPC packet the way ESP Web Tools does: header, rpc, checksum, '\n'.
static size_t rpcPacket(uint8_t* out, uint8_t cmd, const uint8_t* data, uint8_t n) {
  uint8_t i = 0;
  memcpy(out, "IMPROV", 6); i = 6;
  out[i++] = 1; out[i++] = IMPROV_TYPE_RPC; out[i++] = (uint8_t)(n + 2);
  out[i++] = cmd; out[i++] = n;
  memcpy(out + i, data, n); i += n;
  uint8_t sum = 0;
  for (uint8_t k = 0; k < i; k++) sum += out[k];
  out[i++] = sum;
  out[i++] = '\n';
  return i;
}

static ImprovFeed feedAll(const uint8_t* b, size_t n, bool lineStart = true) {
  ImprovFeed last = IMPROV_NOT_MINE;
  for (size_t i = 0; i < n; i++) {
    ImprovFeed f = improvFeed(p, b[i], lineStart && (i == 0 || last == IMPROV_PACKET), 0);
    if (f == IMPROV_PACKET || f == IMPROV_BAD_CHECKSUM) return f;
    last = f;
  }
  return last;
}

void test_get_state_packet_parses() {
  uint8_t pkt[32];
  size_t n = rpcPacket(pkt, IMPROV_CMD_GET_STATE, nullptr, 0);
  TEST_ASSERT_EQUAL_INT(IMPROV_PACKET, feedAll(pkt, n - 1));   // without the '\n'
  TEST_ASSERT_EQUAL_UINT8(IMPROV_TYPE_RPC, improvPacketType(p));
  uint8_t len;
  const uint8_t* d = improvPacketData(p, &len);
  ImprovRpc rpc;
  TEST_ASSERT_TRUE(improvParseRpc(d, len, rpc));
  TEST_ASSERT_EQUAL_UINT8(IMPROV_CMD_GET_STATE, rpc.cmd);
}

void test_wifi_settings_decodes_ssid_and_password() {
  uint8_t data[64] = { 5, 'h', 'o', 'm', 'e', '1', 6, 's', 'e', 'c', 'r', 'e', 't' };
  uint8_t pkt[96];
  size_t n = rpcPacket(pkt, IMPROV_CMD_WIFI_SETTINGS, data, 13);
  TEST_ASSERT_EQUAL_INT(IMPROV_PACKET, feedAll(pkt, n - 1));
  uint8_t len;
  ImprovRpc rpc;
  TEST_ASSERT_TRUE(improvParseRpc(improvPacketData(p, &len), len, rpc));
  TEST_ASSERT_EQUAL_STRING("home1", rpc.ssid);
  TEST_ASSERT_EQUAL_STRING("secret", rpc.pass);
}

void test_bad_checksum_is_reported() {
  uint8_t pkt[32];
  size_t n = rpcPacket(pkt, IMPROV_CMD_GET_INFO, nullptr, 0);
  pkt[n - 2] ^= 0x55;
  TEST_ASSERT_EQUAL_INT(IMPROV_BAD_CHECKSUM, feedAll(pkt, n - 1));
}

void test_json_line_is_not_mine() {
  TEST_ASSERT_EQUAL_INT(IMPROV_NOT_MINE, improvFeed(p, '{', true, 0));
}

void test_i_mid_line_is_not_mine() {
  // 'I' only starts a packet at the beginning of a line.
  TEST_ASSERT_EQUAL_INT(IMPROV_NOT_MINE, improvFeed(p, 'I', false, 0));
}

void test_line_starting_with_i_is_dropped_on_mismatch() {
  TEST_ASSERT_EQUAL_INT(IMPROV_MORE, improvFeed(p, 'I', true, 0));
  TEST_ASSERT_EQUAL_INT(IMPROV_DROPPED, improvFeed(p, 'n', false, 0));
  TEST_ASSERT_EQUAL_INT(IMPROV_NOT_MINE, improvFeed(p, 'f', false, 0));
}

void test_wrong_version_is_dropped() {
  const uint8_t b[] = { 'I', 'M', 'P', 'R', 'O', 'V', 2 };
  TEST_ASSERT_EQUAL_INT(IMPROV_DROPPED, feedAll(b, sizeof(b)));
}

void test_stalled_packet_resets() {
  improvFeed(p, 'I', true, 0);
  improvFeed(p, 'M', false, 10);
  // 'P' long after: the partial packet is abandoned and 'P' isn't a start byte.
  TEST_ASSERT_EQUAL_INT(IMPROV_NOT_MINE, improvFeed(p, 'P', false, 10 + IMPROV_STALL_MS + 1));
}

void test_rpc_with_wrong_inner_length_is_malformed() {
  const uint8_t d[] = { IMPROV_CMD_WIFI_SETTINGS, 9, 3, 'a', 'b' };
  ImprovRpc rpc;
  TEST_ASSERT_FALSE(improvParseRpc(d, sizeof(d), rpc));
}

void test_wifi_settings_with_empty_ssid_is_malformed() {
  const uint8_t d[] = { IMPROV_CMD_WIFI_SETTINGS, 2, 0, 0 };
  ImprovRpc rpc;
  TEST_ASSERT_FALSE(improvParseRpc(d, sizeof(d), rpc));
}

void test_encode_state_round_trips_through_parser() {
  uint8_t out[16];
  size_t n = improvEncodeState(out, sizeof(out), IMPROV_STATE_PROVISIONED);
  TEST_ASSERT_EQUAL_size_t(12, n);
  TEST_ASSERT_EQUAL_UINT8('\n', out[n - 1]);
  TEST_ASSERT_EQUAL_INT(IMPROV_PACKET, feedAll(out, n - 1));
  TEST_ASSERT_EQUAL_UINT8(IMPROV_TYPE_STATE, improvPacketType(p));
}

void test_encode_result_lays_out_strings() {
  const char* strs[] = { "glance", "dev" };
  uint8_t out[64];
  size_t n = improvEncodeResult(out, sizeof(out), IMPROV_CMD_GET_INFO, strs, 2);
  TEST_ASSERT_EQUAL_INT(IMPROV_PACKET, feedAll(out, n - 1));
  uint8_t len;
  const uint8_t* d = improvPacketData(p, &len);
  const uint8_t want[] = { IMPROV_CMD_GET_INFO, 11, 6, 'g', 'l', 'a', 'n', 'c', 'e', 3, 'd', 'e', 'v' };
  TEST_ASSERT_EQUAL_UINT8(sizeof(want), len);
  TEST_ASSERT_EQUAL_MEMORY(want, d, sizeof(want));
}

void test_encode_empty_result() {
  uint8_t out[16];
  size_t n = improvEncodeResult(out, sizeof(out), IMPROV_CMD_WIFI_SETTINGS, nullptr, 0);
  TEST_ASSERT_EQUAL_INT(IMPROV_PACKET, feedAll(out, n - 1));
  uint8_t len;
  const uint8_t* d = improvPacketData(p, &len);
  TEST_ASSERT_EQUAL_UINT8(2, len);
  TEST_ASSERT_EQUAL_UINT8(0, d[1]);
}

void test_encode_too_small_returns_zero() {
  uint8_t out[8];
  TEST_ASSERT_EQUAL_size_t(0, improvEncodeError(out, sizeof(out), IMPROV_ERR_UNKNOWN_RPC));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_get_state_packet_parses);
  RUN_TEST(test_wifi_settings_decodes_ssid_and_password);
  RUN_TEST(test_bad_checksum_is_reported);
  RUN_TEST(test_json_line_is_not_mine);
  RUN_TEST(test_i_mid_line_is_not_mine);
  RUN_TEST(test_line_starting_with_i_is_dropped_on_mismatch);
  RUN_TEST(test_wrong_version_is_dropped);
  RUN_TEST(test_stalled_packet_resets);
  RUN_TEST(test_rpc_with_wrong_inner_length_is_malformed);
  RUN_TEST(test_wifi_settings_with_empty_ssid_is_malformed);
  RUN_TEST(test_encode_state_round_trips_through_parser);
  RUN_TEST(test_encode_result_lays_out_strings);
  RUN_TEST(test_encode_empty_result);
  RUN_TEST(test_encode_too_small_returns_zero);
  return UNITY_END();
}
