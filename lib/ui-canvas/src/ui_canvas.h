#pragma once
#include <stdint.h>
#include <Arduino_GFX_Library.h>

// Atomic-design tokens + atoms for the logical canvas. Everything here is
// "dumb": pure draw functions taking an explicit canvas + props, no globals,
// no app state. Screens (organisms) compose these; the app owns state.

// ── Tokens ──────────────────────────────────────────────────────────────
// TFT_eSPI-style color names (Arduino_GFX calls them RGB565_*). Kept so UI
// code reads naturally.
#define GREEN  0x07E0
#define RED    0xF800
#define BLUE   0x001F
#define YELLOW 0xFFE0
#define WHITE  0xFFFF
#define BLACK  0x0000
#define HOT    0xFA20   // red-orange: warnings, impatience, deny
#define PANEL  0x2104   // overlay panel background

// Face-provided theme: the active character's colorway, used by every screen.
struct Palette {
  uint16_t body, bg, text, textDim, ink;
};

// ── Atoms ───────────────────────────────────────────────────────────────
// Centered text (Arduino_GFX has no setTextDatum). Default font is 6×8 px
// per glyph, multiplied by size.
void uiCenteredText(Arduino_GFX& g, const char* s, int cx, int cy, int sz,
                    uint16_t fg, uint16_t bg);

// Word-wrap into fixed rows (UTF-8 safe hard breaks, continuation indent).
// Returns the number of rows produced.
uint8_t uiWrap(const char* in, char out[][48], uint8_t maxRows, uint8_t width);

// Footer hint row inside an overlay panel: "<downLbl> ↓  <rightLbl> →".
void uiMenuHints(Arduino_GFX& g, const Palette& p, int mx, int mw, int hy,
                 const char* downLbl = "A", const char* rightLbl = "B");

// QR code (version 3, ECC_LOW — fits short payloads like WIFI: strings),
// drawn black-on-white with a quiet zone, horizontally centered on centerX.
// Returns false if the text doesn't fit.
bool uiQr(Arduino_GFX& g, const char* text, int centerX, int topY, int scale);
