#include "pet.h"
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"
#include "character.h"
#include "buddy.h"
#include "stats.h"

static Arduino_GFX* _heartTgt = nullptr;
static void tinyHeart(int x, int y, bool filled, uint16_t col) {
  Arduino_GFX& g = *_heartTgt;
  if (filled) {
    g.fillCircle(x - 2, y, 2, col);
    g.fillCircle(x + 2, y, 2, col);
    g.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
  } else {
    g.drawCircle(x - 2, y, 2, col);
    g.drawCircle(x + 2, y, 2, col);
    g.drawLine(x - 4, y + 1, x, y + 5, col);
    g.drawLine(x + 4, y + 1, x, y + 5, col);
  }
}

static uint8_t petPage = 0;

static void drawPetStats(Arduino_GFX& g, const Palette& p) {
  _heartTgt = &g;
  const int TOP = 70;
  g.fillRect(0, TOP, W, H - TOP, p.bg);
  g.setTextSize(1);
  int y = TOP + 16;

  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_L, y - 2); g.print("mood");
  uint8_t mood = statsMoodTier();
  uint16_t moodCol = (mood >= 3) ? RED : (mood >= 2) ? HOT : p.textDim;
  for (int i = 0; i < 4; i++) tinyHeart(54 + i * 16, y + 2, i < mood, moodCol);

  y += 20;
  g.setCursor(SAFE_L, y - 2); g.print("fed");
  uint8_t fed = statsFedProgress();
  for (int i = 0; i < 10; i++) {
    int px = 38 + i * 9;
    if (i < fed) g.fillCircle(px, y + 1, 2, p.body);
    else g.drawCircle(px, y + 1, 2, p.textDim);
  }

  y += 20;
  g.setCursor(SAFE_L, y - 2); g.print("energy");
  uint8_t en = statsEnergyTier();
  uint16_t enCol = (en >= 4) ? 0x07FF : (en >= 2) ? 0xFFE0 : HOT;
  for (int i = 0; i < 5; i++) {
    int px = 54 + i * 13;
    if (i < en) g.fillRect(px, y - 2, 9, 6, enCol);
    else g.drawRect(px, y - 2, 9, 6, p.textDim);
  }

  y += 24;
  g.fillRoundRect(SAFE_L, y - 2, 42, 14, 3, p.body);
  g.setTextColor(p.bg, p.body);
  g.setCursor(SAFE_L + 5, y + 1); g.printf("Lv %u", stats().level);

  y += 20;
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_L, y);
  g.printf("approved %u", stats().approvals);
  g.setCursor(SAFE_L, y + 10);
  g.printf("denied   %u", stats().denials);
  uint32_t nap = stats().napSeconds;
  g.setCursor(SAFE_L, y + 20);
  g.printf("napped   %luh%02lum", nap/3600, (nap/60)%60);
  auto tokFmt = [&](const char* label, uint32_t v, int yPx) {
    g.setCursor(SAFE_L, yPx);
    if (v >= 1000000)   g.printf("%s%lu.%luM", label, v/1000000, (v/100000)%10);
    else if (v >= 1000) g.printf("%s%lu.%luK", label, v/1000, (v/100)%10);
    else                g.printf("%s%lu", label, v);
  };
  tokFmt("tokens   ", stats().tokens, y + 30);
  tokFmt("today    ", tama.tokensToday, y + 40);
}

static void drawPetHowTo(Arduino_GFX& g, const Palette& p) {
  const int TOP = 70;
  g.fillRect(0, TOP, W, H - TOP, p.bg);
  g.setTextSize(1);
  int y = TOP + 2;
  auto ln = [&](uint16_t c, const char* s) {
    g.setTextColor(c, p.bg); g.setCursor(SAFE_L, y); g.print(s); y += 9;
  };
  auto gap = [&]() { y += 4; };

  y += 12;  // room for the PET header drawn by drawPet()

  ln(p.body,    "MOOD");
  ln(p.textDim, " approve fast = up");
  ln(p.textDim, " deny lots = down"); gap();

  ln(p.body,    "FED");
  ln(p.textDim, " 50K tokens =");
  ln(p.textDim, " level up + confetti"); gap();

  ln(p.body,    "ENERGY");
  ln(p.textDim, " face-down to nap");
  ln(p.textDim, " refills to full"); gap();

  ln(p.textDim, "idle 30s = off");
  ln(p.textDim, "any button = wake"); gap();

  ln(p.textDim, "A: screens  B: page");
  ln(p.textDim, "hold A: menu");
}

void petDraw(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  int y = 70;

  if (petPage == 0) drawPetStats(g, p);
  else drawPetHowTo(g, p);

  // Header on top of whichever page drew — title left, counter right
  g.setTextSize(1);
  g.setTextColor(p.text, p.bg);
  g.setCursor(SAFE_L, y + 2);
  if (ownerName()[0]) {
    g.printf("%s's %s", ownerName(), petName());
  } else {
    g.print(petName());
  }
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_R - 24, y + 2);
  g.printf("%u/%u", petPage + 1, PET_PAGES);
}


uint8_t petPageIdx() { return petPage; }
void petSetPage(uint8_t p) { petPage = p % PET_PAGES; }
void petNextPage() { petPage = (petPage + 1) % PET_PAGES; }
