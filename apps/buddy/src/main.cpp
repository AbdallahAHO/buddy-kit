// Arduino_GFX's font headers reference an undefined U8G2_FONT_SECTION
// macro — provide a no-op so the const array compiles. The font symbol
// itself is gated on U8G2_USE_LARGE_FONTS (set in build_flags).
#define U8G2_FONT_SECTION(name)
#include <Arduino_GFX_Library.h>

#include "hw/hw.h"
#include <LittleFS.h>
#include <stdarg.h>
#include <esp_mac.h>
#include "ble_bridge.h"
#include "agent_link.h"
#include "buddy.h"

// spr is a thin alias for hwCanvas() — keeps existing UI code unchanged
#define spr (*hwCanvas())

// Advertise as "Claude-XXXX" (last two BT MAC bytes) so multiple sticks
// in one room are distinguishable in the desktop picker. Name persists in
// btName for the BLUETOOTH info page.
char btName[16] = "Claude";
static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

#include "character.h"
#include "stats.h"
#include "file_push.h"
#include "app_commands.h"
#include "app_state.h"
#include "screens/home.h"
#include "screens/info.h"
#include "screens/pet.h"
#include "screens/passkey.h"
#include "screens/wifi_setup.h"
#include "wifi_link.h"
#include "ui_canvas.h"
// LED replaced by AMOLED border-flash via hwBorderAlert() — no GPIO LED.

// PersonaState comes from agent_state.h (via agent_link.h).
const char* stateNames[] = { "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart" };

TamaState    tama;
PersonaState baseState   = P_SLEEP;
PersonaState activeState = P_SLEEP;
uint32_t     oneShotUntil = 0;
uint32_t     lastShakeCheck = 0;
float        accelBaseline = 1.0f;
unsigned long t = 0;

// Menu
bool    menuOpen    = false;
uint8_t menuSel     = 0;
uint8_t brightLevel = 4;           // 0..4 → ScreenBreath 20..100
bool    btnALong    = false;

uint8_t displayMode = DISP_NORMAL;
char     lastPromptId[40] = "";
uint32_t lastInteractMs = 0;
bool     dimmed = false;
bool     screenOff = false;
bool     swallowBtnA = false;
bool     swallowBtnB = false;
bool     buddyMode = false;
bool     gifAvailable = false;
const uint8_t SPECIES_GIF = 0xFF;   // species NVS sentinel: use the installed GIF

// Cycle GIF (if installed) → ASCII species 0..N-1 → GIF. Persisted to the
// existing "species" NVS key; 0xFF means GIF mode.
static void nextPet() {
  uint8_t n = buddySpeciesCount();
  if (!buddyMode) {                          // GIF → species 0
    buddyMode = true;
    buddySetSpeciesIdx(0);
    speciesIdxSave(0);
  } else if (buddySpeciesIdx() + 1 >= n && gifAvailable) {  // last species → GIF
    buddyMode = false;
    speciesIdxSave(SPECIES_GIF);
  } else {                                   // species i → species i+1
    buddyNextSpecies();
  }
  characterInvalidate();
  if (buddyMode) buddyInvalidate();
}

static void prevPet() {
  uint8_t n = buddySpeciesCount();
  if (!buddyMode) {                          // GIF → last species
    buddyMode = true;
    buddySetSpeciesIdx(n - 1);
    speciesIdxSave(n - 1);
  } else if (buddySpeciesIdx() == 0 && gifAvailable) {      // first species → GIF
    buddyMode = false;
    speciesIdxSave(SPECIES_GIF);
  } else {
    buddyPrevSpecies();
  }
  characterInvalidate();
  if (buddyMode) buddyInvalidate();
}
uint32_t wakeTransitionUntil = 0;
const uint32_t SCREEN_OFF_MS    = 30UL * 1000UL;        // 30s on battery, non-clock idle
const uint32_t CLOCK_OFF_MS_BAT = 5UL  * 60UL * 1000UL; // 5min on battery, clock visible

bool     napping = false;
uint32_t napStartMs = 0;
uint32_t promptArrivedMs = 0;

// Face-down = Z-axis dominant and negative. Debounced so a toss doesn't count.
static bool isFaceDown() {
  float ax, ay, az;
  hwImuAccel(&ax, &ay, &az);
  return az < -0.7f && fabsf(ax) < 0.4f && fabsf(ay) < 0.4f;
}

static void applyBrightness() { hwDisplayBrightness(brightLevel); }

void wake() {
  lastInteractMs = millis();
  if (screenOff) {
    hwDisplaySleep(false);
    applyBrightness();
    screenOff = false;
    wakeTransitionUntil = millis() + 12000;
  }
  if (dimmed) { applyBrightness(); dimmed = false; }
}
bool     responseSent = false;

static void beep(uint16_t freq, uint16_t dur) {
  if (settings().sound) hwBeep(freq, dur);
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

// Replies (permission decisions) fan out to every transport — USB, BLE,
// and the HTTP hub — via the shared LineOut.
static void sendCmd(const char* json) {
  gLineOut.write((const uint8_t*)json, strlen(json));
  gLineOut.write((const uint8_t*)"\n", 1);
}

void applyDisplayMode() {
  bool peek = displayMode != DISP_NORMAL;
  characterSetPeek(peek);
  buddySetPeek(peek);
  // Clear the whole sprite on mode switch. drawInfo/drawPet clear their
  // own regions when they run, but when you switch FROM info/pet TO normal,
  // those functions stop running and their stale pixels stay behind. Full
  // clear is cheap and guarantees no leftovers between modes.
  spr.fillScreen(0x0000);
  characterInvalidate();  // redraws character on next tick (text mode path)
}

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

const char* menuItems[] = { "settings", "turn off", "help", "about", "demo", "close" };
const uint8_t MENU_N = 6;

bool    wifiSetupOpen = false;   // QR pairing screen (drawWifiSetup)
bool    settingsOpen = false;
uint8_t settingsSel  = 0;
const char* settingsItems[] = { "brightness", "sound", "bluetooth", "wifi", "wifi setup", "led", "transcript", "clock rot", "ascii pet", "reset", "back" };
const uint8_t SETTINGS_N = 11;

bool    resetOpen = false;
uint8_t resetSel  = 0;
const char* resetItems[] = { "delete char", "forget wifi", "factory reset", "back" };
const uint8_t RESET_N = 4;
static uint32_t resetConfirmUntil = 0;
static uint8_t  resetConfirmIdx = 0xFF;

static void applySetting(uint8_t idx) {
  Settings& s = settings();
  switch (idx) {
    case 0:
      brightLevel = (brightLevel + 1) % 5;
      applyBrightness();
      return;
    case 1: s.sound = !s.sound; break;
    case 2:
      // BT toggle is a stored preference only — BLE stays live. Turning
      // BLE off cleanly would require tearing down the BLE stack which
      // the Arduino BLE library doesn't do reliably. If we need a
      // hard-off someday, stop advertising via BLEDevice::getAdvertising().
      s.bt = !s.bt;
      break;
    case 3:
      s.wifi = !s.wifi;
      if (s.wifi && wifiLinkHasCreds()) wifiLinkConnect();
      else if (!s.wifi) wifiLinkDisconnect();
      break;
    case 4:   // wifi setup: QR pairing screen + captive portal
      settingsOpen = false;
      wifiLinkStartPortal();
      wifiSetupOpen = true;
      return;
    case 5: s.led = !s.led; break;
    case 6: s.hud = !s.hud; break;
    case 7: s.clockRot = (s.clockRot + 1) % 3; break;
    case 8: nextPet(); return;
    case 9: resetOpen = true; resetSel = 0; resetConfirmIdx = 0xFF; return;
    case 10: settingsOpen = false; characterInvalidate(); return;
  }
  settingsSave();
}

// Tap-twice confirm: first tap arms (label flips to "really?"), second
// within 3s executes. Scrolling away clears the arm.
static void applyReset(uint8_t idx) {
  uint32_t now = millis();
  bool armed = (resetConfirmIdx == idx) && (int32_t)(now - resetConfirmUntil) < 0;

  if (idx == 3) { resetOpen = false; return; }

  if (!armed) {
    resetConfirmIdx = idx;
    resetConfirmUntil = now + 3000;
    beep(1400, 60);
    return;
  }

  beep(800, 200);
  if (idx == 1) {
    // forget wifi: creds gone, radio off — no reboot needed
    wifiLinkForget();
    resetOpen = false;
    characterInvalidate();
    return;
  }
  if (idx == 0) {
    // delete char: wipe /characters/, reboot into ASCII mode
    File d = LittleFS.open("/characters");
    if (d && d.isDirectory()) {
      File e;
      while ((e = d.openNextFile())) {
        char path[80];
        snprintf(path, sizeof(path), "/characters/%s", e.name());
        if (e.isDirectory()) {
          File f;
          while ((f = e.openNextFile())) {
            char fp[128];
            snprintf(fp, sizeof(fp), "%s/%s", path, f.name());
            f.close();
            LittleFS.remove(fp);
          }
          e.close();
          LittleFS.rmdir(path);
        } else {
          e.close();
          LittleFS.remove(path);
        }
      }
      d.close();
    }
  } else {
    // factory reset: NVS namespace wipe + filesystem format + BLE bonds.
    // Clears stats, owner, petname, species, settings, GIF characters,
    // and any stored LTKs so the next desktop has to re-pair.
    statsFactoryWipe();
    LittleFS.format();
    bleClearBonds();
  }
  delay(300);
  ESP.restart();
}

const int MENU_HINT_H = 14;   // panels add this to height for uiMenuHints

static void drawSettings() {
  const Palette& p = characterPalette();
  int mw = 118, mh = 16 + SETTINGS_N * 14 + MENU_HINT_H;
  int mx = (W - mw) / 2, my = (H - mh) / 2;
  spr.fillRoundRect(mx, my, mw, mh, 4, PANEL);
  spr.drawRoundRect(mx, my, mw, mh, 4, p.textDim);
  spr.setTextSize(1);
  Settings& s = settings();
  bool vals[] = { s.sound, s.bt, s.wifi, s.led, s.hud };
  for (int i = 0; i < SETTINGS_N; i++) {
    bool sel = (i == settingsSel);
    spr.setTextColor(sel ? p.text : p.textDim, PANEL);
    spr.setCursor(mx + 6, my + 8 + i * 14);
    spr.print(sel ? "> " : "  ");
    spr.print(settingsItems[i]);
    spr.setCursor(mx + mw - 36, my + 8 + i * 14);
    spr.setTextColor(p.textDim, PANEL);
    if (i == 0) {
      spr.printf("%u/4", brightLevel);
    } else if (i >= 1 && i <= 3) {
      spr.setTextColor(vals[i-1] ? GREEN : p.textDim, PANEL);
      spr.print(vals[i-1] ? " on" : "off");
    } else if (i == 4) {
      WifiLinkState ws = wifiLinkState();
      if (ws == WIFI_LINK_ONLINE) { spr.setTextColor(GREEN, PANEL); spr.print(" up"); }
      else if (ws == WIFI_LINK_JOINING) spr.print("...");
    } else if (i == 5 || i == 6) {
      spr.setTextColor(vals[i-2] ? GREEN : p.textDim, PANEL);
      spr.print(vals[i-2] ? " on" : "off");
    } else if (i == 7) {
      static const char* const RN[] = { "auto", "port", "land" };
      spr.print(RN[s.clockRot]);
    } else if (i == 8) {
      uint8_t total = buddySpeciesCount() + (gifAvailable ? 1 : 0);
      uint8_t pos   = buddyMode ? buddySpeciesIdx() + 1 : total;
      spr.printf("%u/%u", pos, total);
    }
  }
  uiMenuHints(spr, p, mx, mw, my + mh - 12, "Next", "Change");
}

static void drawReset() {
  const Palette& p = characterPalette();
  int mw = 118, mh = 16 + RESET_N * 14 + MENU_HINT_H;
  int mx = (W - mw) / 2, my = (H - mh) / 2;
  spr.fillRoundRect(mx, my, mw, mh, 4, PANEL);
  spr.drawRoundRect(mx, my, mw, mh, 4, HOT);
  spr.setTextSize(1);
  for (int i = 0; i < RESET_N; i++) {
    bool sel = (i == resetSel);
    spr.setTextColor(sel ? p.text : p.textDim, PANEL);
    spr.setCursor(mx + 6, my + 8 + i * 14);
    spr.print(sel ? "> " : "  ");
    bool armed = (i == resetConfirmIdx) &&
                 (int32_t)(millis() - resetConfirmUntil) < 0;
    if (armed) spr.setTextColor(HOT, PANEL);
    spr.print(armed ? "really?" : resetItems[i]);
  }
  uiMenuHints(spr, p, mx, mw, my + mh - 12);
}

void menuConfirm() {
  switch (menuSel) {
    case 0: settingsOpen = true; menuOpen = false; settingsSel = 0; break;
    case 1: hwPowerOff(); break;
    case 2:
    case 3:
      menuOpen = false;
      displayMode = DISP_INFO;
      infoSetPage((menuSel == 2) ? INFO_PG_BUTTONS : INFO_PG_CREDITS);
      applyDisplayMode();
      characterInvalidate();
      break;
    case 4: dataSetDemo(!dataDemo()); break;
    case 5: menuOpen = false; characterInvalidate(); break;
  }
}

void drawMenu() {
  const Palette& p = characterPalette();
  int mw = 118, mh = 16 + MENU_N * 14 + MENU_HINT_H;
  int mx = (W - mw) / 2, my = (H - mh) / 2;
  spr.fillRoundRect(mx, my, mw, mh, 4, PANEL);
  spr.drawRoundRect(mx, my, mw, mh, 4, p.textDim);
  spr.setTextSize(1);
  for (int i = 0; i < MENU_N; i++) {
    bool sel = (i == menuSel);
    spr.setTextColor(sel ? p.text : p.textDim, PANEL);
    spr.setCursor(mx + 6, my + 8 + i * 14);
    spr.print(sel ? "> " : "  ");
    spr.print(menuItems[i]);
    if (i == 4) spr.print(dataDemo() ? "  on" : "  off");
  }
  uiMenuHints(spr, p, mx, mw, my + mh - 12);
}

void triggerOneShot(PersonaState s, uint32_t durMs) {
  activeState = s;
  oneShotUntil = millis() + durMs;
}

bool checkShake() {
  float ax, ay, az;
  hwImuAccel(&ax, &ay, &az);
  float mag = sqrtf(ax*ax + ay*ay + az*az);
  float delta = fabsf(mag - accelBaseline);
  accelBaseline = accelBaseline * 0.95f + mag * 0.05f;
  return delta > 0.8f;
}




void setup() {
  // Native USB (HWCDC) delivers a whole ~370-byte chunk line faster than the
  // loop polls, and its default 256-byte RX ring silently drops the tail.
  // Size it for several chunk lines so USB file push works like BLE does.
  Serial.setRxBufferSize(2048);
  hwInit();                  // Wire + expander + display + power + input + IMU + RTC + audio
  startBt();                 // BLE stays always-on
  appCommandsInit();         // ack fan-out (USB+BLE) + file-push sink
  wifiLinkInit();
  applyBrightness();
  lastInteractMs = millis();
  statsLoad();
  settingsLoad();
  if (settings().wifi && wifiLinkHasCreds()) wifiLinkConnect();
  petNameLoad();
  buddyInit();

  characterInit(nullptr);    // scan /characters/ for whatever is installed
  gifAvailable = characterLoaded();
  // species NVS: 0..N-1 = ASCII species, 0xFF = use GIF (also the default,
  // so a fresh install lands on the GIF).
  buddyMode = !(gifAvailable && speciesIdxLoad() == SPECIES_GIF);
  applyDisplayMode();

  {
    const Palette& p = characterPalette();
    spr.fillScreen(p.bg);
    if (ownerName()[0]) {
      char line[40];
      snprintf(line, sizeof(line), "%s's", ownerName());
      uiCenteredText(spr, line,      W/2, H/2 - 12, 2, p.text, p.bg);
      uiCenteredText(spr, petName(), W/2, H/2 + 12, 2, p.body, p.bg);
    } else {
      uiCenteredText(spr, "Hello!",          W/2, H/2 - 12, 2, p.body,    p.bg);
      uiCenteredText(spr, "a buddy appears", W/2, H/2 + 12, 1, p.textDim, p.bg);
    }
    spr.setTextSize(1);
    hwDisplayPush();
    delay(1800);
  }

  Serial.printf("buddy: %s\n", buddyMode ? "ASCII mode" : "GIF character loaded");
}

void loop() {
  hwInputUpdate();
  wifiLinkTick();
  // A successful join turns the wifi setting on, so provisioning via the
  // portal or cmd survives reboot without a separate toggle step.
  static WifiLinkState _lastWifiState = WIFI_LINK_OFF;
  {
    WifiLinkState ws = wifiLinkState();
    if (ws == WIFI_LINK_ONLINE && _lastWifiState != WIFI_LINK_ONLINE && !settings().wifi) {
      settings().wifi = true;
      settingsSave();
    }
    _lastWifiState = ws;
  }
  // Pairing screen auto-closes shortly after the join succeeds.
  static uint32_t _wifiOnlineSince = 0;
  if (wifiSetupOpen && wifiLinkState() == WIFI_LINK_ONLINE) {
    if (!_wifiOnlineSince) _wifiOnlineSince = millis();
    else if (millis() - _wifiOnlineSince > 5000) { _wifiOnlineSince = 0; wifiSetupClose(); }
  } else {
    _wifiOnlineSince = 0;
  }
  ;
  t++;
  uint32_t now = millis();

  dataPoll(&tama);
  if (statsPollLevelUp()) triggerOneShot(P_CELEBRATE, 3000);
  baseState = agentDerive(tama);

  // After waking the screen, hold sleep for 12s so users see the wake-up
  // animation. Urgent states (attention, celebrate, busy) override this.
  if (baseState == P_IDLE && (int32_t)(now - wakeTransitionUntil) < 0) baseState = P_SLEEP;

  if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState;

  // Attention indicator: AMOLED red border flash (replaces M5StickC LED).
  hwBorderAlert(activeState == P_ATTENTION && settings().led
                && (now / 400) % 2 == 0);

  // shake → dizzy + force scenario advance
  if (now - lastShakeCheck > 50) {
    lastShakeCheck = now;
    if (!menuOpen && !screenOff && checkShake() && (int32_t)(now - oneShotUntil) >= 0) {
      wake();
      triggerOneShot(P_DIZZY, 2000);
      Serial.println("shake: dizzy");
    }
  }

  // BtnA: step through fake scenarios
  // Prompt arrival: beep, reset response flag
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId)-1);
    lastPromptId[sizeof(lastPromptId)-1] = 0;
    responseSent = false;
    if (tama.promptId[0]) {
      promptArrivedMs = millis();
      wake();
      beep(1200, 80);   // alert chirp
      // Jump to the approval screen no matter what was open — drawApproval
      // only runs from drawHUD which only runs in DISP_NORMAL.
      displayMode = DISP_NORMAL;
      menuOpen = settingsOpen = resetOpen = false;
      applyDisplayMode();
      characterInvalidate();
      if (buddyMode) buddyInvalidate();
    }
  }

  bool inPrompt = tama.promptId[0] && !responseSent;

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
    if (wifiSetupOpen) { wifiSetupClose(); }
    else if (resetOpen) { resetOpen = false; }
    else if (settingsOpen) { settingsOpen = false; characterInvalidate(); }
    else {
      menuOpen = !menuOpen;
      menuSel = 0;
      if (!menuOpen) characterInvalidate();
    }
    Serial.println(menuOpen ? "menu open" : "menu close");
  }
  if (hwBtnA().wasReleased) {
    if (!btnALong && !swallowBtnA) {
      if (wifiSetupOpen) {
        beep(1800, 30);
        wifiSetupClose();
      } else if (inPrompt) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
        sendCmd(cmd);
        responseSent = true;
        uint32_t tookS = (millis() - promptArrivedMs) / 1000;
        statsOnApproval(tookS);
        beep(2400, 60);
        if (tookS < 5) triggerOneShot(P_HEART, 2000);
      } else if (resetOpen) {
        beep(1800, 30);
        resetSel = (resetSel + 1) % RESET_N;
        resetConfirmIdx = 0xFF;
      } else if (settingsOpen) {
        beep(1800, 30);
        settingsSel = (settingsSel + 1) % SETTINGS_N;
      } else if (menuOpen) {
        beep(1800, 30);
        menuSel = (menuSel + 1) % MENU_N;
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
    if (wifiSetupOpen) {
      beep(1800, 30);
      wifiSetupClose();
    } else if (inPrompt) {
      char cmd[96];
      snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", tama.promptId);
      sendCmd(cmd);
      responseSent = true;
      statsOnDenial();
      beep(600, 60);
    } else if (resetOpen) {
      beep(2400, 30);
      applyReset(resetSel);
    } else if (settingsOpen) {
      beep(2400, 30);
      applySetting(settingsSel);
    } else if (menuOpen) {
      beep(2400, 30);
      menuConfirm();
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
                 && !menuOpen && !settingsOpen && !resetOpen && !inPrompt && !wifiSetupOpen
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
  } else if (menuOpen || settingsOpen || resetOpen) {
    // Tap a menu row → directly select + confirm. Reuses the layout
    // constants from drawMenu/drawSettings/drawReset.
    int n      = menuOpen ? MENU_N : settingsOpen ? SETTINGS_N : RESET_N;
    int hint   = MENU_HINT_H;
    int mw     = 118;
    int mh     = 16 + n * 14 + hint;
    int mx     = (W - mw) / 2;
    int my     = (H - mh) / 2;
    int rowH   = 14;
    int rowsTop = my + 8;
    const HwTouch& t = hwTouch();
    if (t.justPressed && t.x >= mx && t.x < mx + mw &&
        t.y >= rowsTop && t.y < rowsTop + n * rowH) {
      int hit = (t.y - rowsTop) / rowH;
      if (hit >= 0 && hit < n) {
        beep(2400, 30);
        if (menuOpen)         { menuSel     = hit; menuConfirm(); }
        else if (settingsOpen){ settingsSel = hit; applySetting(hit); }
        else /* resetOpen */  { resetSel    = hit; applyReset(hit); }
      }
    }
  }
  // END of press-based approval/overlay taps. Below: release-based classifier
  // for DISP_NORMAL / DISP_PET / DISP_INFO (vertical swipe cycles mode,
  // horizontal swipe in clock mode cycles species, stationary tap routes
  // to region-specific actions). Approval and overlay menus are excluded
  // so an accidental drag can't mis-decide.

  if (tp.justReleased
      && !inPrompt && !menuOpen && !settingsOpen && !resetOpen
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

  // blink bookkeeping

  // Charging clock: takes over the home screen when on USB power, no
  // overlays, no prompt, no live Claude data, and the RTC has been set
  // by the bridge. Pet sleeps underneath. Exit restores Y via
  // applyDisplayMode() so the next mode-switch isn't visually offset.
  homeClockTick();     // 1Hz internal throttle; also caches USB presence
  // Show the clock when nothing is happening — bridge heartbeat alone
  // doesn't count as activity (it's the only way to get the RTC synced).
  // Clock shows when Claude is idle and the RTC is synced — regardless
  // of USB power. On battery the screen still auto-offs after a longer
  // timeout (CLOCK_OFF_MS_BAT) so it doesn't drain forever.
  bool clocking = displayMode == DISP_NORMAL
               && !menuOpen && !settingsOpen && !resetOpen && !inPrompt
               && tama.sessionsRunning == 0 && tama.sessionsWaiting == 0
               && dataRtcValid();
  // Portrait-only clock on AMOLED port; landscape was removed.
  static bool wasClocking = false;
  if (clocking != wasClocking) {
    if (clocking) {
      // GIFs are tall (up to 140 px) — must shrink to fit above clock.
      // ASCII buddy at scale 2 reaches y≈126; clock starts at y=140
      // (compact single-line layout) so peek isn't needed and the pet
      // gets to keep its full size.
      characterSetPeek(true);
      buddySetPeek(false);
      // Clear the full canvas once on entry: buddy/clock both update
      // partial regions every frame, so any stale ink left behind from
      // the previous mode would persist forever.
      const Palette& cp = characterPalette();
      spr.fillScreen(cp.bg);
    } else {
      applyDisplayMode();
    }
    characterInvalidate();
    if (buddyMode) buddyInvalidate();
    wasClocking = clocking;
  }
  // Skip the time-of-day mood logic while a one-shot animation
  // (shake → dizzy, level-up → celebrate, fast-approve → heart) is
  // active — otherwise it would overwrite activeState immediately.
  if (clocking && (int32_t)(now - oneShotUntil) >= 0) {
    if ((int32_t)(now - _playfulUntil) < 0) {
      // Recently interacted with (pet tap / species swipe) — rotate through
      // awake animations instead of falling back to the time-of-day logic
      // that mostly picks P_SLEEP. Decays to normal after PLAYFUL_MS.
      static const PersonaState PLAYFUL[] = {
        P_IDLE, P_IDLE, P_HEART, P_IDLE, P_CELEBRATE, P_IDLE
      };
      activeState = PLAYFUL[(now / 5000) % 6];
    } else {
      // Ambient rhythm is SLEEP↔IDLE only. Emotional states (HEART, CELEBRATE,
      // DIZZY) are reactions — they fire from real events (shake, fast-approve,
      // level-up, pet tap, species swipe) via triggerOneShot / playful window,
      // never spontaneously from wall-clock mood.
      uint8_t h = homeClockHour();
      if (h < 7 || h >= 22) activeState = (now/15000 % 8 == 0) ? P_IDLE  : P_SLEEP;
      else                  activeState = (now/12000 % 6 == 0) ? P_SLEEP : P_IDLE;
    }
  }

  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && !lastPasskey) { wake(); beep(1800, 60); }
  lastPasskey = pk;

  if (napping || screenOff) {
    // skip canvas render — face-down or powered off
  } else if (buddyMode) {
    buddyTick(activeState);
  } else if (characterLoaded()) {
    characterSetState(activeState);
    characterTick();
  } else {
    const Palette& p = characterPalette();
    spr.fillScreen(p.bg);
    spr.setTextColor(p.textDim, p.bg);
    spr.setTextSize(1);
    if (filePushActive()) {
      uint32_t done = filePushProgress(), total = filePushTotal();
      spr.setCursor(SAFE_L, 90);
      spr.print("installing");
      spr.setCursor(SAFE_L, 102);
      spr.printf("%luK / %luK", done/1024, total/1024);
      int barW = W - 16;
      spr.drawRect(SAFE_L, 116, barW, 8, p.textDim);
      if (total > 0) {
        int fill = (int)((uint64_t)barW * done / total);
        if (fill > 1) spr.fillRect(SAFE_L + 1, 117, fill - 1, 6, p.body);
      }
    } else {
      spr.setCursor(SAFE_L, 100);
      spr.print("no character loaded");
    }
  }
  if (!napping && !screenOff) {
    if (blePasskey()) passkeyDraw(spr);
    else if (wifiSetupOpen) wifiSetupDraw(spr);
    else if (clocking) homeClockDraw(spr);
    else if (displayMode == DISP_INFO) infoDraw(spr);
    else if (displayMode == DISP_PET) petDraw(spr);
    else if (settings().hud) homeHudDraw(spr);
    if (resetOpen) drawReset();
    else if (settingsOpen) drawSettings();
    else if (menuOpen) drawMenu();
    hwDisplayPush();
  }

  // Face-down nap: dim immediately, pause animations, accumulate sleep time.
  // Skipped during approval — you're holding it to read, not sleeping it.
  // Exit needs sustained not-down so IMU noise at the threshold doesn't
  // bounce brightness between 8 and full every few frames.
  static int8_t faceDownFrames = 0;
  if (!inPrompt) {
    bool down = isFaceDown();
    if (down)       { if (faceDownFrames < 20) faceDownFrames++; }
    else            { if (faceDownFrames > -10) faceDownFrames--; }
  }

  if (!napping && faceDownFrames >= 15) {
    napping = true;
    napStartMs = now;
    hwDisplayBrightness(0);
    dimmed = true;
  } else if (napping && faceDownFrames <= -8) {
    napping = false;
    statsOnNapEnd((now - napStartMs) / 1000);
    statsOnWake();
    wake();
  }

  // millis() not the cached `now`: wake() runs after `now` is captured,
  // so now - lastInteractMs underflows when a button is held → flicker.
  // Auto-off rules:
  //   USB plugged: never (clock can stay visible indefinitely)
  //   Battery + clock visible: 5 min (CLOCK_OFF_MS_BAT)
  //   Battery + non-clock idle: 30 s (SCREEN_OFF_MS)
  if (!screenOff && !inPrompt && !homeOnUsb()) {
    uint32_t idleMs    = millis() - lastInteractMs;
    uint32_t threshold = clocking ? CLOCK_OFF_MS_BAT : SCREEN_OFF_MS;
    if (idleMs > threshold) {
      hwDisplaySleep(true);
      screenOff = true;
    }
  }

  // AMOLED burn-in mitigation: every 5 min force a full canvas redraw.
  // OLED pixels degrade where they stay lit at constant value; redrawing
  // (rather than incremental updates) at least exercises every pixel for
  // a frame. A more aggressive 1-px shimmy could shift the whole canvas
  // each cycle, but this minimum is a safe baseline.
  static uint32_t lastShimmy = 0;
  if (millis() - lastShimmy > 5UL * 60UL * 1000UL) {
    lastShimmy = millis();
    characterInvalidate();
    if (buddyMode) buddyInvalidate();
  }

  // LTPO-lite: vary loop cadence by what's happening. Animations tick on
  // wall-clock (buddy.cpp TICK_MS=200) and redraws are gated, so slowing the
  // loop during ambient SLEEP↔IDLE costs no frames — just fewer MCU wakes.
  // Fast rate only where latency is felt: input, interactive UI, one-shots,
  // nap-exit, transfer progress, BLE pairing.
  uint32_t loopMs;
  if (screenOff) {
    loopMs = 200;
  } else if (napping
          || hwTouch().down
          || hwBtnA().isPressed || hwBtnB().isPressed
          || inPrompt || menuOpen || settingsOpen || resetOpen
          || (int32_t)(now - oneShotUntil) < 0
          || filePushActive()
                || wifiSetupOpen
          || blePasskey()) {
    loopMs = 16;
  } else {
    loopMs = 100;
  }
  // Slice the idle sleep so a touch-down IRQ (edge-triggered) or a button
  // press breaks out within ~8ms instead of waiting the full loopMs. Without
  // this, first-tap latency during idle felt sluggish.
  if (loopMs <= 16) {
    delay(loopMs);
  } else {
    uint32_t slept = 0;
    while (slept < loopMs) {
      uint32_t slice = (loopMs - slept > 8) ? 8 : (loopMs - slept);
      delay(slice);
      slept += slice;
      if (hwTouchIrqPending()) break;
      if (digitalRead(PIN_KEY1) == LOW) break;
    }
  }
}
