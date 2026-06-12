#include "input_router.h"
#include <Arduino.h>
#include "app_state.h"
#include "app_commands.h"
#include "overlays.h"
#include "stats.h"
#include "character.h"
#include "buddy.h"
#include "ble_bridge.h"
#include "hw/hw.h"
#include "screens/home.h"
#include "screens/info.h"
#include "screens/pet.h"
#include "screens/wifi_setup.h"

static bool btnALong  = false;
static bool swallowBtnA = false;
static bool swallowBtnB = false;

// Replies (permission decisions) fan out to every transport — USB, BLE,
// and the HTTP hub — via the shared LineOut.
static void sendCmd(const char* json) {
  gLineOut.write((const uint8_t*)json, strlen(json));
  gLineOut.write((const uint8_t*)"\n", 1);
}


// Touch hit-test helper (additive: keys still work, touch is a 2nd path).
// Returns true on a fresh tap-down inside the rect; releases don't fire.
static bool tap(int x, int y, int w, int h) {
  const HwTouch& t = hwTouch();
  return t.justPressed && t.x >= x && t.y >= y && t.x < x+w && t.y < y+h;
}

// Press-start snapshot for gesture classification (swipe). Updated on every
// justPressed; read on justReleased to compute Δx/Δy/Δt.
static int16_t  _tpStartX = 0, _tpStartY = 0;
static uint32_t _tpStartMs = 0;

// Rect hit-test against the press-START position. Use on justReleased after
// a gesture has been classified as a stationary tap — so a tap with minor
// finger drift still targets the region the user pressed on.
static bool tappedFrom(int x, int y, int w, int h) {
  return _tpStartX >= x && _tpStartY >= y &&
         _tpStartX <  x + w && _tpStartY <  y + h;
}

// After a user interaction in clock mode (pet tap or species swipe), keep the
// buddy awake for this long — otherwise the time-of-day logic snaps back to
// P_SLEEP the instant the one-shot animation expires.
static const uint32_t PLAYFUL_MS = 3UL * 60UL * 1000UL;
static uint32_t _playfulUntil = 0;


// Swipe cycles through all 9 pages as a flat list:
//   Normal → Pet 1/2 → Pet 2/2 → Info 1/6 → … → Info 6/6 → (wrap to Normal)
// Key1 short-press keeps the coarser 3-mode cycle; these helpers are only
// wired into the release-based gesture classifier below.
// applyDisplayMode() fires on mode transitions and Pet sub-page (matches
// existing BtnB behaviour). Info sub-page skips it because drawInfo() clears
// its own region — also matches existing BtnB behaviour.
static void swipeNextPage() {
  if (displayMode == DISP_NORMAL) {
    displayMode = DISP_PET; petSetPage(0);                applyDisplayMode();
  } else if (displayMode == DISP_PET) {
    if (petPageIdx() + 1 < PET_PAGES) { petNextPage();    applyDisplayMode(); }
    else { displayMode = DISP_INFO; infoSetPage(0);       applyDisplayMode(); }
  } else { /* DISP_INFO */
    if (infoPageIdx() + 1 < INFO_PAGES) { infoNextPage(); }
    else { displayMode = DISP_NORMAL;                     applyDisplayMode(); }
  }
}

static void swipePrevPage() {
  if (displayMode == DISP_NORMAL) {
    displayMode = DISP_INFO; infoSetPage(INFO_PAGES - 1); applyDisplayMode();
  } else if (displayMode == DISP_PET) {
    if (petPageIdx() > 0)           { petSetPage(petPageIdx() - 1); applyDisplayMode(); }
    else { displayMode = DISP_NORMAL;                     applyDisplayMode(); }
  } else { /* DISP_INFO */
    if (infoPageIdx() > 0)          { infoSetPage(infoPageIdx() - 1); }
    else { displayMode = DISP_PET; petSetPage(PET_PAGES - 1); applyDisplayMode(); }
  }
}


bool inputPlayful(uint32_t now) { return (int32_t)(now - _playfulUntil) < 0; }

void inputRoute(uint32_t now, bool inPrompt) {
// Button-press wake. Track which button woke the screen so its full
// press cycle (including long-press) is swallowed — you don't want
// BtnA-to-wake to also cycle displayMode or open the menu.
if (hwBtnA().isPressed || hwBtnB().isPressed) {
  if (screenOff) {
    if (hwBtnA().isPressed) swallowBtnA = true;
    if (hwBtnB().isPressed) swallowBtnB = true;
  }
  wake();
}

// Key3 long-press (~1s, AXP IRQ 0x04) toggles screen off — replaces
// M5StickC's PWR short-press behaviour (we only have 2 buttons; short
// press of Key3 is BtnB, so screen-toggle moves to long-press).
// Very-long-press (6s) still powers off via AXP hardware.
if (hwAxpBtnEvent() == 0x04) {
  if (screenOff) {
    wake();
  } else {
    hwDisplaySleep(true);
    screenOff = true;
  }
}

if (hwBtnA().pressedFor(600) && !btnALong && !swallowBtnA) {
  btnALong = true;
  beep(800, 60);
  if (wifiSetupOpen) { wifiSetupClose(); } else
  if (overlayActive()) { overlayClose(); }
  else { overlayOpen(MENU_OVERLAY); }
  Serial.println(overlayActive() ? "menu open" : "menu close");
}
if (hwBtnA().wasReleased) {
  if (!btnALong && !swallowBtnA) {
    if (wifiSetupOpen) { beep(1800, 30); wifiSetupClose(); } else
    if (inPrompt) {
      char cmd[96];
      snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
      sendCmd(cmd);
      responseSent = true;
      uint32_t tookS = (millis() - promptArrivedMs) / 1000;
      statsOnApproval(tookS);
      beep(2400, 60);
      if (tookS < 5) triggerOneShot(P_HEART, 2000);
    } else if (overlayActive()) {
      beep(1800, 30);
      overlayNext();
    } else {
      beep(1800, 30);
      displayMode = (displayMode + 1) % DISP_COUNT;
      applyDisplayMode();
    }
  }
  btnALong = false;
  swallowBtnA = false;
}

// BtnB: pet → heart
if (hwBtnB().wasPressed) {
  if (swallowBtnB) { swallowBtnB = false; }
  else
  if (wifiSetupOpen) { beep(1800, 30); wifiSetupClose(); } else
  if (inPrompt) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", tama.promptId);
    sendCmd(cmd);
    responseSent = true;
    statsOnDenial();
    beep(600, 60);
  } else if (overlayActive()) {
    beep(2400, 30);
    overlayConfirm();
  } else if (displayMode == DISP_INFO) {
    beep(2400, 30);
    infoNextPage();
  } else if (displayMode == DISP_PET) {
    beep(2400, 30);
    petNextPage();
    applyDisplayMode();
  } else {
    beep(2400, 30);
    homeBumpScroll();
  }
}

// ─── Touch (additive — buttons above already handled) ──────────────
// Clocking = idle home screen with RTC synced; drives gesture routing:
// HUD (!clocking) gets tap-to-pet, clocking gets horizontal-swipe-to-switch-species.
bool tpClocking = displayMode == DISP_NORMAL
               && !overlayActive() && !inPrompt
               && !wifiSetupOpen
               && tama.sessionsRunning == 0 && tama.sessionsWaiting == 0
               && dataRtcValid();

const HwTouch& tp = hwTouch();
if (tp.justPressed) { _tpStartX = tp.x; _tpStartY = tp.y; _tpStartMs = millis(); }

if (wifiSetupOpen && tp.justPressed) { beep(1800, 30); wifiSetupClose(); }

// Approval: tap upper half of the approval area = approve,
//           tap lower half = deny.
if (inPrompt) {
  const int APPROVAL_TOP = H - 78;
  if (tap(0, APPROVAL_TOP,      W, 39)) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
    sendCmd(cmd);
    responseSent = true;
    uint32_t tookS = (millis() - promptArrivedMs) / 1000;
    statsOnApproval(tookS);
    beep(2400, 60);
    if (tookS < 5) triggerOneShot(P_HEART, 2000);
  }
  if (tap(0, APPROVAL_TOP + 39, W, 39)) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", tama.promptId);
    sendCmd(cmd);
    responseSent = true;
    statsOnDenial();
    beep(600, 60);
  }
} else if (overlayActive()) {
  // Tap a row → directly select + confirm (geometry mirrors uiListPanel).
  const HwTouch& t = hwTouch();
  if (t.justPressed && overlayTapRow(t.x, t.y)) beep(2400, 30);
}
// END of press-based approval/overlay taps. Below: release-based classifier
// for DISP_NORMAL / DISP_PET / DISP_INFO (vertical swipe cycles mode,
// horizontal swipe in clock mode cycles species, stationary tap routes
// to region-specific actions). Approval and overlay menus are excluded
// so an accidental drag can't mis-decide.

if (tp.justReleased
    && !inPrompt && !overlayActive()
    && !napping && !screenOff) {
  int dx = (int)tp.x - _tpStartX;
  int dy = (int)tp.y - _tpStartY;
  uint32_t dt = millis() - _tpStartMs;

  if (abs(dy) >= 40 && abs(dy) > abs(dx) * 2 && dt < 500) {
    // Vertical swipe → advance one step in the flat 9-page cycle
    // (up = next, down = previous). Key1 keeps the coarser 3-mode cycle.
    beep(1800, 30);
    if (dy < 0) swipeNextPage();
    else        swipePrevPage();
  }
  else if (tpClocking && abs(dx) >= 40 && abs(dx) > abs(dy) * 2 && dt < 500) {
    // Horizontal swipe in clock mode → cycle species (unchanged behaviour).
    beep(2400, 30);
    if (dx > 0) nextPet(); else prevPet();
    _playfulUntil = millis() + PLAYFUL_MS;
  }
  else if (abs(dx) < 12 && abs(dy) < 12 && dt < 800) {
    // Stationary tap → route by press-start position.
    if (displayMode == DISP_INFO && tappedFrom(W - 60, 0, 60, 70)) {
      beep(2400, 30);
      infoNextPage();
    }
    else if (displayMode == DISP_PET && tappedFrom(W - 60, 0, 60, 70)) {
      beep(2400, 30);
      petNextPage();
      applyDisplayMode();
    }
    else if (displayMode == DISP_NORMAL && !tpClocking && tappedFrom(12, 20, W - 24, 110)) {
      // Tap buddy body → heart reaction (HUD; clock mode uses the block below).
      triggerOneShot(P_HEART, 2000);
      _playfulUntil = millis() + PLAYFUL_MS;
      characterInvalidate();
      if (buddyMode) buddyInvalidate();
      beep(2400, 50);
    }
    else if (displayMode == DISP_NORMAL && !tpClocking && tappedFrom(0, H - 32, W, 32)) {
      // Bottom strip → scroll transcript back (mirrors BtnB short-press).
      beep(2400, 30);
      homeBumpScroll();
    }
    else if (tpClocking && _tpStartY < 130) {
      // Clock mode upper half = buddy region (lower half is clock digits).
      triggerOneShot(P_HEART, 2000);
      _playfulUntil = millis() + PLAYFUL_MS;
      characterInvalidate();
      if (buddyMode) buddyInvalidate();
      beep(2400, 50);
    }
  }
}
}
