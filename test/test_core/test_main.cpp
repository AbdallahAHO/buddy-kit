// Native unit tests for the pure libs (run on the host via `pio test -e native_test`).
// These compile with zero Arduino dependency — proof that lib/agent-state and
// lib/line-bus are genuinely Arduino-free (ADR 002).
#include <unity.h>
#include <string.h>
#include "agent_state.h"
#include "line_bus.h"

// agent-state's link-time entropy seam. On-device it's esp_random(); here a
// fixed value keeps tests deterministic (only msg matrixify would call it, and
// these tests use ASCII, so it's never actually invoked).
extern "C" uint32_t agentStateRandom() { return 42; }

void setUp() {}
void tearDown() {}

// ── agentDerive: the persona selector truth table ──────────────────────────
static AgentState stateWith(bool connected, uint8_t running, uint8_t waiting, bool completed) {
  AgentState s; memset(&s, 0, sizeof(s));
  s.connected = connected;
  s.sessionsRunning = running;
  s.sessionsWaiting = waiting;
  s.recentlyCompleted = completed;
  return s;
}

void test_persona_disconnected_is_idle() {
  TEST_ASSERT_EQUAL_INT(P_IDLE, agentDerive(stateWith(false, 5, 5, true)));  // disconnect wins
}
void test_persona_waiting_is_attention() {
  TEST_ASSERT_EQUAL_INT(P_ATTENTION, agentDerive(stateWith(true, 1, 1, false)));
}
void test_persona_completed_is_celebrate() {
  TEST_ASSERT_EQUAL_INT(P_CELEBRATE, agentDerive(stateWith(true, 1, 0, true)));
}
void test_persona_three_running_is_busy() {
  TEST_ASSERT_EQUAL_INT(P_BUSY, agentDerive(stateWith(true, 3, 0, false)));
}
void test_persona_idle_when_quiet() {
  TEST_ASSERT_EQUAL_INT(P_IDLE, agentDerive(stateWith(true, 1, 0, false)));
}
void test_persona_priority_waiting_over_busy() {
  // waiting outranks a busy running count
  TEST_ASSERT_EQUAL_INT(P_ATTENTION, agentDerive(stateWith(true, 9, 2, true)));
}

// ── LineFramer: newline-delimited framing ───────────────────────────────────
static const char* feedStr(LineFramer<32>& f, const char* s) {
  const char* last = nullptr;
  for (; *s; s++) { const char* line = f.feed(*s); if (line) last = line; }
  return last;
}

void test_framer_emits_line_on_newline() {
  LineFramer<32> f;
  TEST_ASSERT_NULL(f.feed('h'));
  TEST_ASSERT_NULL(f.feed('i'));
  const char* line = f.feed('\n');
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_STRING("hi", line);
}
void test_framer_blank_line_is_null() {
  LineFramer<32> f;
  TEST_ASSERT_NULL(f.feed('\n'));   // empty line yields nothing
}
void test_framer_carriage_return_terminates() {
  LineFramer<32> f;
  const char* line = feedStr(f, "ok\r");
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_STRING("ok", line);
}
void test_framer_truncates_overlong() {
  LineFramer<8> f;                  // holds 7 chars + NUL
  const char* line = nullptr;
  for (const char* s = "abcdefghij"; *s; s++) { const char* l = f.feed(*s); if (l) line = l; }
  line = f.feed('\n');
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_EQUAL_STRING("abcdefg", line);   // truncated, not split
}

// ── agentApplyJson: snapshot + time-sync application ────────────────────────
void test_apply_snapshot_updates_state() {
  JsonDocument doc;
  doc["total"] = 4; doc["running"] = 3; doc["waiting"] = 0;
  doc["completed"] = false; doc["tokens"] = 12345; doc["msg"] = "working";
  AgentState s; memset(&s, 0, sizeof(s)); s.connected = true;
  AgentEvent ev = agentApplyJson(doc, s, 1000);
  TEST_ASSERT_EQUAL_INT(AgentEvent::STATE_UPDATED, ev.kind);
  TEST_ASSERT_EQUAL_UINT8(3, s.sessionsRunning);
  TEST_ASSERT_TRUE(ev.hasTokens);
  TEST_ASSERT_EQUAL_UINT32(12345, ev.tokens);
  TEST_ASSERT_EQUAL_STRING("working", s.msg);
  TEST_ASSERT_EQUAL_UINT32(1000, s.lastUpdated);
  TEST_ASSERT_EQUAL_INT(P_BUSY, agentDerive(s));   // 3 running → busy
}
void test_apply_time_sync() {
  JsonDocument doc;
  doc["time"][0] = 1700000000u; doc["time"][1] = 3600;
  AgentState s; memset(&s, 0, sizeof(s));
  AgentEvent ev = agentApplyJson(doc, s, 0);
  TEST_ASSERT_EQUAL_INT(AgentEvent::TIME_SYNC, ev.kind);
  TEST_ASSERT_EQUAL_UINT32(1700000000u, ev.epoch);
  TEST_ASSERT_EQUAL_INT32(3600, ev.tzOffsetSec);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_persona_disconnected_is_idle);
  RUN_TEST(test_persona_waiting_is_attention);
  RUN_TEST(test_persona_completed_is_celebrate);
  RUN_TEST(test_persona_three_running_is_busy);
  RUN_TEST(test_persona_idle_when_quiet);
  RUN_TEST(test_persona_priority_waiting_over_busy);
  RUN_TEST(test_framer_emits_line_on_newline);
  RUN_TEST(test_framer_blank_line_is_null);
  RUN_TEST(test_framer_carriage_return_terminates);
  RUN_TEST(test_framer_truncates_overlong);
  RUN_TEST(test_apply_snapshot_updates_state);
  RUN_TEST(test_apply_time_sync);
  return UNITY_END();
}
