#pragma once
#include <stdint.h>
#include "ui_canvas.h"

// Overlay menus as data: one ListPanel molecule + three row tables (main
// menu, settings, reset). Selection/arm state and the open-overlay pointer
// live here; input_router drives next/confirm/tap, main renders via
// overlayDraw when overlayActive().

struct OverlayItem {
  const char* label;
  // Optional value column: write text into buf, set *color. nullptr = none.
  void (*value)(char* buf, size_t cap, uint16_t* color);
  // Confirm action. Return true to keep the overlay open (toggles), false
  // to close it (navigation, one-shot actions handle their own close).
  bool (*confirm)(uint8_t idx);
  bool armToConfirm;   // destructive rows: first confirm arms ("really?")
};

struct Overlay {
  const OverlayItem* items;
  uint8_t n;
  uint16_t borderColor;   // 0 = palette textDim
  const char* hintA;
  const char* hintB;
};

extern const Overlay MENU_OVERLAY, SETTINGS_OVERLAY, RESET_OVERLAY;

bool overlayActive();
void overlayOpen(const Overlay& o);
void overlayClose();
void overlayNext();              // BtnA short: move selection (clears arm)
void overlayConfirm();           // BtnB / tap: run the selected row
bool overlayTapRow(int x, int y); // touch: row hit → select + confirm
void overlayDraw(Arduino_GFX& g);
