#include "ui_canvas.h"
#include <string.h>
#include "qrcode.h"

void uiCenteredText(Arduino_GFX& g, const char* s, int cx, int cy, int sz,
                    uint16_t fg, uint16_t bg) {
  int w = (int)strlen(s) * 6 * sz;
  int h = 8 * sz;
  g.setTextSize(sz);
  g.setTextColor(fg, bg);
  g.setCursor(cx - w/2, cy - h/2);
  g.print(s);
}

static uint8_t _utf8SafeTake(const char* w, uint8_t take, uint8_t wlen) {
  if (take == 0 || take >= wlen) return take;
  while (take > 0 && ((uint8_t)w[take] & 0xC0) == 0x80) take--;
  return take;
}

uint8_t uiWrap(const char* in, char out[][48], uint8_t maxRows, uint8_t width) {
  uint8_t row = 0, col = 0;
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;                     // skip leading spaces
    // measure next word
    const char* w = p;
    while (*p && *p != ' ') p++;
    uint8_t wlen = p - w;
    if (wlen == 0) break;
    uint8_t need = (col > 0 ? 1 : 0) + wlen;
    if (col + need > width) {
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;              // continuation indent
    }
    if (col > 1 || (col == 1 && out[row][0] != ' ')) out[row][col++] = ' ';
    else if (col == 1 && row > 0) {}           // already have the indent space
    // hard-break words that still don't fit, on UTF-8 char boundaries
    while (wlen > width - col) {
      uint8_t take = _utf8SafeTake(w, width - col, wlen);
      if (take == 0) take = 1;                 // safety: avoid infinite loop
      memcpy(&out[row][col], w, take); col += take; w += take; wlen -= take;
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    memcpy(&out[row][col], w, wlen); col += wlen;
  }
  if (col > 0 && row < maxRows) { out[row][col] = 0; row++; }
  return row;
}

void uiMenuHints(Arduino_GFX& g, const Palette& p, int mx, int mw, int hy,
                 const char* downLbl, const char* rightLbl) {
  g.drawFastHLine(mx + 6, hy - 4, mw - 12, p.textDim);
  g.setTextColor(p.textDim, PANEL);
  // 6px/glyph at size 1; triangle goes 4px after the label ends
  int x = mx + 8;
  g.setCursor(x, hy); g.print(downLbl);
  x += strlen(downLbl) * 6 + 4;
  g.fillTriangle(x, hy + 1, x + 6, hy + 1, x + 3, hy + 6, p.textDim);
  x = mx + mw / 2 + 4;
  g.setCursor(x, hy); g.print(rightLbl);
  x += strlen(rightLbl) * 6 + 4;
  g.fillTriangle(x, hy, x, hy + 6, x + 5, hy + 3, p.textDim);
}

bool uiQr(Arduino_GFX& g, const char* text, int centerX, int topY, int scale) {
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(3)];
  if (qrcode_initText(&qr, qrData, 3, ECC_LOW, text) != 0) return false;
  const int quiet = 6;
  int qrPx = qr.size * scale;
  int x0 = centerX - qrPx / 2;
  g.fillRect(x0 - quiet, topY - quiet, qrPx + 2*quiet, qrPx + 2*quiet, WHITE);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        g.fillRect(x0 + x*scale, topY + y*scale, scale, scale, BLACK);
  return true;
}
