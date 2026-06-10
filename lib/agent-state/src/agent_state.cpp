#include "agent_state.h"
#include <string.h>

PersonaState agentDerive(const AgentState& s) {
  if (!s.connected)            return P_IDLE;
  if (s.sessionsWaiting > 0)   return P_ATTENTION;
  if (s.recentlyCompleted)     return P_CELEBRATE;
  if (s.sessionsRunning >= 3)  return P_BUSY;
  return P_IDLE;   // connected, 0+ sessions, nothing urgent — hang out
}

// Replace non-ASCII bytes (UTF-8 multi-byte sequences for Chinese, etc.)
// with random "Matrix-rain"-flavoured ASCII glyphs. The font is ASCII-only,
// so otherwise these bytes render as garbage. One Chinese char (3 bytes)
// becomes 3 random ASCII chars — fits the digital-rain look.
static void _matrixify(char* s) {
  static const char POOL[] = "01<>{}[]/\\|*~$%#@&=+-_:;.!?ABCDEFGHJKLMNPQRSTVWXYZ";
  static const int  POOL_N = sizeof(POOL) - 1;
  for (; *s; s++) {
    if ((uint8_t)*s > 127) *s = POOL[agentStateRandom() % POOL_N];
  }
}

AgentEvent agentApplyJson(JsonDocument& doc, AgentState& out, uint32_t nowMs) {
  AgentEvent ev;

  // Bridge sends {"time":[epoch_sec, tz_offset_sec]} once on connect.
  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    ev.kind = AgentEvent::TIME_SYNC;
    ev.epoch = t[0].as<uint32_t>();
    ev.tzOffsetSec = (int32_t)t[1];
    return ev;
  }

  out.sessionsTotal     = doc["total"]     | out.sessionsTotal;
  out.sessionsRunning   = doc["running"]   | out.sessionsRunning;
  out.sessionsWaiting   = doc["waiting"]   | out.sessionsWaiting;
  out.recentlyCompleted = doc["completed"] | false;
  if (doc["tokens"].is<uint32_t>()) {
    ev.hasTokens = true;
    ev.tokens = doc["tokens"] | 0;
  }
  out.tokensToday = doc["tokens_today"] | out.tokensToday;
  const char* m = doc["msg"];
  if (m) {
    strncpy(out.msg, m, sizeof(out.msg)-1); out.msg[sizeof(out.msg)-1]=0;
    _matrixify(out.msg);
  }
  JsonArray la = doc["entries"];
  if (!la.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : la) {
      if (n >= 8) break;
      const char* s = v.as<const char*>();
      strncpy(out.lines[n], s ? s : "", 91); out.lines[n][91]=0;
      // Transcript renders with a CJK u8g2 font (drawHUD), so leave the
      // raw UTF-8 bytes intact — only msg/promptTool/promptHint stay
      // ASCII-rendered and need matrixify.
      n++;
    }
    if (n != out.nLines || (n > 0 && strcmp(out.lines[n-1], out.msg) != 0)) {
      out.lineGen++;
    }
    out.nLines = n;
  }
  JsonObject pr = doc["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
    strncpy(out.promptId,   pid ? pid : "", sizeof(out.promptId)-1);   out.promptId[sizeof(out.promptId)-1]=0;
    strncpy(out.promptTool, pt  ? pt  : "", sizeof(out.promptTool)-1); out.promptTool[sizeof(out.promptTool)-1]=0;
    strncpy(out.promptHint, ph  ? ph  : "", sizeof(out.promptHint)-1); out.promptHint[sizeof(out.promptHint)-1]=0;
    _matrixify(out.promptTool);
    _matrixify(out.promptHint);
    // Don't matrixify promptId — it's an opaque token, must echo verbatim.
  } else {
    out.promptId[0] = 0; out.promptTool[0] = 0; out.promptHint[0] = 0;
  }
  out.lastUpdated = nowMs;
  ev.kind = AgentEvent::STATE_UPDATED;
  return ev;
}
