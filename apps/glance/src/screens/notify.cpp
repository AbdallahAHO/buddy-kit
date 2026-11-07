// The notify surface: the head of the inbox (title, wrapped body, action
// buttons) drawn over the whole canvas while anything is pending. It paints
// every pixel each frame and statusDraw does the same, so closing it is a
// full clear by construction (ghost rule). Draw reads gNotify, never writes.
#include <Arduino.h>
#include "../glance_state.h"
#include "ui_canvas.h"
#include "hw/hw.h"

static const uint16_t AMBER = 0xFD20;
static const uint16_t DIM   = 0x8410;
static const int HEADER_H   = 18;
static const int BTN_H      = 24;
static const int BTN_GAP    = 4;
static const int FOOTER_Y   = SAFE_B - 9;

static uint16_t _accent(const NotifyItem& it) { return it.kind == NK_APPROVAL ? RED : AMBER; }

void notifyScreenTick() {
  uint32_t now = millis();
  notifyExpire(gNotify, now);
  const NotifyItem* it = notifyHead(gNotify);

  if (it && notifyMarkSeen(gNotify)) {
    char line[96];
    size_t n = notifyAckLine(line, sizeof(line), it->id, "seen");
    if (n) gLineOut.write((const uint8_t*)line, n);
  }

  // LED: a 1.6 s triangle pulse while pending, written only on change.
  static uint8_t lastLevel = 0;
  static bool lastRed = false;
  uint8_t level = 0;
  bool red = it && it->kind == NK_APPROVAL;
  if (it) {
    uint32_t ph = now % 1600;
    uint32_t tri = ph < 800 ? ph : 1600 - ph;    // 0..800
    level = (uint8_t)(1 + tri * 15 / 800);       // 1..16
  }
  if (level == lastLevel && red == lastRed) return;
  lastLevel = level; lastRed = red;
  uint8_t k = level * 6;                         // peak ~96: a WS2812 at 255 glares
  if (red) hwLed(k, 0, 0);
  else     hwLed(k, (uint8_t)(k * 2 / 5), 0);
}

// The panel font is 6x8 ASCII; anything else renders as junk glyphs.
static void _ascii(char* s) {
  for (; *s; s++) if ((uint8_t)*s < 0x20 || (uint8_t)*s > 0x7E) *s = '?';
}

static uint8_t _cols(int sz) {
  int c = SAFE_W / (6 * sz);
  return (uint8_t)(c > 47 ? 47 : c);
}

static const char* _kindLabel(NotifyKind k) {
  switch (k) {
    case NK_NOTIFICATION: return "NOTIFICATION";
    case NK_APPROVAL:     return "APPROVAL";
    case NK_QUESTION:     return "QUESTION";
    default:              return "MESSAGE";
  }
}

static void _drawButton(Arduino_GFX& g, int x, int y, int w, const char* label,
                        bool selected, uint16_t accent) {
  char s[NOTIFY_ACTION_MAX + 1];
  strncpy(s, label, sizeof(s) - 1); s[sizeof(s) - 1] = 0;
  _ascii(s);
  int sz = (int)strlen(s) * 12 <= w - 8 ? 2 : 1;
  uint16_t bg = selected ? accent : BLACK;
  if (selected) g.fillRoundRect(x, y, w, BTN_H, 4, accent);
  else          g.drawRoundRect(x, y, w, BTN_H, 4, DIM);
  uiCenteredText(g, s, x + w / 2, y + BTN_H / 2, sz, selected ? BLACK : WHITE, bg);
}

void notifyScreenDraw(Arduino_GFX& g) {
  const NotifyItem* it = notifyHead(gNotify);
  if (!it) return;
  uint16_t accent = _accent(*it);
  g.fillScreen(BLACK);

  // Header strip: kind + queue position.
  g.fillRect(SAFE_L, SAFE_T, SAFE_W, HEADER_H, accent);
  g.setTextSize(1);
  g.setTextColor(BLACK, accent);
  g.setCursor(SAFE_L + 4, SAFE_T + 5);
  g.print(_kindLabel(it->kind));
  if (gNotify.n > 1) {
    char pos[8]; snprintf(pos, sizeof(pos), "1/%u", gNotify.n);
    g.setCursor(SAFE_R - 4 - (int)strlen(pos) * 6, SAFE_T + 5);
    g.print(pos);
  }
  int y = SAFE_T + HEADER_H + 6;

  // Title: size 2, at most two rows.
  char rows[10][48];
  uint8_t n = uiWrap(it->title, rows, 2, _cols(2));
  g.setTextSize(2);
  g.setTextColor(WHITE, BLACK);
  for (uint8_t i = 0; i < n; i++) {
    _ascii(rows[i]);
    g.setCursor(SAFE_L, y); g.print(rows[i]); y += 18;
  }
  y += 4;

  // Actions grid: two per row, a single action spans the width.
  uint8_t na = it->nActions;
  uint8_t btnRows = na == 0 ? 0 : (na + 1) / 2;
  int actionsTop = FOOTER_Y - 4 - btnRows * (BTN_H + BTN_GAP);
  int halfW = (SAFE_W - BTN_GAP) / 2;
  for (uint8_t i = 0; i < na; i++) {
    int bx = SAFE_L + (i % 2) * (halfW + BTN_GAP);
    int by = actionsTop + (i / 2) * (BTN_H + BTN_GAP);
    _drawButton(g, bx, by, na == 1 ? SAFE_W : halfW, it->actions[i], i == gNotify.sel, accent);
  }

  // Body: size 2 when it fits the space left, else size 1.
  int bodyH = actionsTop - 6 - y;
  uint8_t maxRows2 = bodyH > 0 ? (uint8_t)(bodyH / 18) : 0;
  uint8_t maxRows1 = bodyH > 0 ? (uint8_t)(bodyH / 10) : 0;
  if (maxRows2 > 10) maxRows2 = 10;
  if (maxRows1 > 10) maxRows1 = 10;
  int sz = 2, lineH = 18;
  n = uiWrap(it->body, rows, 10, _cols(2));
  if (n > maxRows2) {
    sz = 1; lineH = 10;
    n = uiWrap(it->body, rows, maxRows1, _cols(1));
  }
  g.setTextSize(sz);
  g.setTextColor(0xC618, BLACK);
  for (uint8_t i = 0; i < n; i++) {
    _ascii(rows[i]);
    g.setCursor(SAFE_L, y); g.print(rows[i]); y += lineH;
  }

  // Footer: what the button does here.
  g.setTextSize(1);
  g.setTextColor(DIM, BLACK);
  const char* hint = na ? "press: next   hold: choose"
                   : it->kind == NK_NOTIFICATION ? "hold: OK" : "hold: dismiss";
  g.setCursor(SAFE_L, FOOTER_Y); g.print(hint);
}
