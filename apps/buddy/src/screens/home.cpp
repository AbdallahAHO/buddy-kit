// drawHUD uses a U8g2 CJK font through Arduino_GFX — the no-op section
// macro must exist before the GFX header (same hack as upstream main.cpp).
#define U8G2_FONT_SECTION(name)
#include "home.h"
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"
#include "character.h"
#include "stats.h"
#include "agent_link.h"
#include "../overlays.h"
#include "hw/hw.h"

// Portrait-only clock on AMOLED port (landscape removed — 368×448 is
// near-square; rotating doesn't change the layout meaningfully).
static HwTime  _clkTm;
uint32_t       _clkLastRead = 0;   // zeroed by agent_link.h on time-sync
static bool    _onUsb       = false;
void homeClockTick() {
  if (millis() - _clkLastRead < 1000) return;
  _clkLastRead = millis();
  _onUsb = hwBattery().usbPresent;
  hwRtcRead(&_clkTm);
}

// Clock face: shown when charging on USB with nothing else going on.
// Paints the upper ~110px to the canvas; pet renders below.
static const char* const MON[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char* const DOW[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

static uint8_t clockDow() { return _clkTm.dow % 7; }
void homeClockDraw(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  char hms[12]; snprintf(hms, sizeof(hms), "%02u:%02u:%02u", _clkTm.H, _clkTm.M, _clkTm.S);
  uint8_t mi = (_clkTm.Mo >= 1 && _clkTm.Mo <= 12) ? _clkTm.Mo - 1 : 0;
  char dl[16]; snprintf(dl, sizeof(dl), "%s %s %02u", DOW[clockDow()], MON[mi], _clkTm.D);

  // Compact clock: single-line HH:MM:SS plus date below. Clears only
  // y >= 140 so the buddy at full home scale (reaches y≈126) fits
  // entirely above. Wider canvas + portrait orientation has plenty of
  // horizontal room for HH:MM:SS at size 3 (8 chars × 18 = 144 px).
  g.fillRect(0, 140, W, H - 140, p.bg);
  uiCenteredText(g, hms, CX, 160, 3, p.text,    p.bg);
  uiCenteredText(g, dl,  CX, SAFE_B - 21, 1, p.textDim, p.bg);
  g.setTextSize(1);
}

static void drawApproval(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  const int AREA = 78;
  g.fillRect(0, H - AREA, W, AREA, p.bg);
  g.drawFastHLine(0, H - AREA, W, p.textDim);

  g.setTextSize(1);
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_L, H - AREA + 4);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  if (waited >= 10) g.setTextColor(HOT, p.bg);
  g.printf("approve? %lus", (unsigned long)waited);

  // Size 2 only if it fits one line (~10 chars at 12px on 135px screen)
  int toolLen = strlen(tama.promptTool);
  g.setTextColor(p.text, p.bg);
  g.setTextSize(toolLen <= 10 ? 2 : 1);
  g.setCursor(SAFE_L, H - AREA + (toolLen <= 10 ? 14 : 18));
  g.print(tama.promptTool);
  g.setTextSize(1);

  // Hint wraps at ~21 chars to two lines under the tool name
  g.setTextColor(p.textDim, p.bg);
  int hlen = strlen(tama.promptHint);
  g.setCursor(SAFE_L, H - AREA + 34);
  g.printf("%.21s", tama.promptHint);
  if (hlen > 21) {
    g.setCursor(SAFE_L, H - AREA + 42);
    g.printf("%.21s", tama.promptHint + 21);
  }

  if (responseSent) {
    g.setTextColor(p.textDim, p.bg);
    g.setCursor(SAFE_L, SAFE_B - 12);
    g.print("sent...");
  } else {
    g.setTextColor(GREEN, p.bg);
    g.setCursor(SAFE_L, SAFE_B - 12);
    g.print("A: approve");
    g.setTextColor(HOT, p.bg);
    g.setCursor(SAFE_R - 48, SAFE_B - 12);
    g.print("B: deny");
  }
}

static uint8_t  msgScroll = 0;
static uint16_t lastLineGen = 0;

void homeHudDraw(Arduino_GFX& g) {
  if (tama.promptId[0]) { drawApproval(g); return; }
  const Palette& p = characterPalette();
  // chill7 font: glyphs ~7 px tall but baseline-positioned (setCursor
  // is the baseline, not the top). Allow ~10 px line spacing, ~22 byte
  // budget per line — Chinese chars are ~7 px wide, ASCII ~5 px, so a
  // mixed line of 22 bytes (~7 Chinese OR 22 ASCII) fits W=184.
  const int SHOW = 3, LH = 10, WIDTH = 22;
  const int AREA = SHOW * LH + 4;
  g.fillRect(0, H - AREA, W, AREA, p.bg);

  // Menu/settings/reset should hide the HUD strip underneath — panels are
  // centered and don't cover the bottom 34 px on their own.
  if (overlayActive()) return;

  if (tama.lineGen != lastLineGen) { msgScroll = 0; lastLineGen = tama.lineGen; wake(); }

  // buddy/character ticks leave textsize at 2 (home scale); without
  // pinning it here the CJK font alternates between 1× and 2× every tick.
  g.setTextSize(1);
  g.setFont((const uint8_t*)u8g2_font_chill7_h_cjk);

  if (tama.nLines == 0) {
    g.setTextColor(p.text, p.bg);
    g.setCursor(SAFE_L, SAFE_B - 4);
    g.print(tama.msg);
    g.setFont((const GFXfont*)NULL);
    return;
  }

  static char disp[32][48];
  static uint8_t srcOf[32];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < tama.nLines && nDisp < 32; i++) {
    uint8_t got = uiWrap(tama.lines[i], &disp[nDisp], 32 - nDisp, WIDTH);
    for (uint8_t j = 0; j < got; j++) srcOf[nDisp + j] = i;
    nDisp += got;
  }

  uint8_t maxBack = (nDisp > SHOW) ? (nDisp - SHOW) : 0;
  if (msgScroll > maxBack) msgScroll = maxBack;

  int end = (int)nDisp - msgScroll;
  int start = end - SHOW; if (start < 0) start = 0;
  uint8_t newest = tama.nLines - 1;
  for (int i = 0; start + i < end; i++) {
    uint8_t row = start + i;
    bool fresh = (srcOf[row] == newest) && (msgScroll == 0);
    g.setTextColor(fresh ? p.text : p.textDim, p.bg);
    g.setCursor(SAFE_L, H - AREA + 8 + i * LH);   // +8 = baseline offset for 7-px font
    g.print(disp[row]);
  }

  g.setFont((const GFXfont*)NULL);

  if (msgScroll > 0) {
    g.setTextSize(1);
    g.setTextColor(p.body, p.bg);
    g.setCursor(SAFE_R - 18, SAFE_B - 10);
    g.printf("-%u", msgScroll);
  }
}


uint8_t homeClockHour() { return _clkTm.H; }
bool homeOnUsb() { return _onUsb; }
void homeBumpScroll() { msgScroll = (msgScroll >= 30) ? 0 : msgScroll + 1; }
