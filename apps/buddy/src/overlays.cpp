#include "overlays.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "app_state.h"
#include "stats.h"
#include "character.h"
#include "buddy.h"
#include "ble_bridge.h"
#include "wifi_link.h"
#include "hw/hw.h"
#include "screens/info.h"

static const Overlay* _active = nullptr;
static uint8_t  _sel = 0;
static uint8_t  _armedIdx = 0xFF;
static uint32_t _armedUntil = 0;

bool overlayActive() { return _active != nullptr; }

void overlayOpen(const Overlay& o) {
  _active = &o;
  _sel = 0;
  _armedIdx = 0xFF;
}

void overlayClose() {
  _active = nullptr;
  _armedIdx = 0xFF;
  // Full repaint: the panel overdrew a band that neither the buddy nor the
  // HUD owns, so partial invalidation leaves ghost pixels behind.
  applyDisplayMode();
  if (buddyMode) buddyInvalidate();
}

void overlayNext() {
  if (!_active) return;
  _sel = (_sel + 1) % _active->n;
  _armedIdx = 0xFF;   // scrolling away disarms
}

void overlayConfirm() {
  if (!_active) return;
  const OverlayItem& it = _active->items[_sel];
  // Tap-twice confirm: first confirm arms (label flips to "really?"),
  // second within 3s executes.
  if (it.armToConfirm) {
    bool armed = (_armedIdx == _sel) && (int32_t)(millis() - _armedUntil) < 0;
    if (!armed) {
      _armedIdx = _sel;
      _armedUntil = millis() + 3000;
      beep(1400, 60);
      return;
    }
    beep(800, 200);
  }
  if (!it.confirm(_sel)) overlayClose();
}

bool overlayTapRow(int x, int y) {
  if (!_active) return false;
  int mx = uiListPanelX(W), rowsTop = uiListRowsTop(H, _active->n);
  if (x < mx || x >= mx + UI_LIST_W) return false;
  if (y < rowsTop || y >= rowsTop + _active->n * UI_LIST_ROW_H) return false;
  _sel = (y - rowsTop) / UI_LIST_ROW_H;
  overlayConfirm();
  return true;
}

void overlayDraw(Arduino_GFX& g) {
  if (!_active) return;
  const Palette& p = characterPalette();
  static char vbuf[12][12];
  static ListRowProps rows[12];
  uint8_t n = _active->n > 12 ? 12 : _active->n;
  for (uint8_t i = 0; i < n; i++) {
    const OverlayItem& it = _active->items[i];
    bool armed = it.armToConfirm && i == _armedIdx &&
                 (int32_t)(millis() - _armedUntil) < 0;
    rows[i].label = armed ? "really?" : it.label;
    rows[i].value = nullptr;
    if (!armed && it.value) {
      uint16_t col = p.textDim;
      it.value(vbuf[i], sizeof(vbuf[i]), &col);
      if (vbuf[i][0]) { rows[i].value = vbuf[i]; rows[i].valueColor = col; }
    }
  }
  ListPanelProps props = {
    rows, n, _sel,
    _active->borderColor ? _active->borderColor : p.textDim,
    _active->hintA, _active->hintB, &p
  };
  uiListPanel(g, W, H, props);
}

// ---------------------------------------------------------------------------
// Row tables. Confirm fns return true to stay open (toggles), false to close.
// ---------------------------------------------------------------------------

// -- settings rows --

static void vBright(char* b, size_t c, uint16_t*) { snprintf(b, c, "%u/4", brightLevel); }
static void vOnOff(char* b, size_t c, uint16_t* col, bool v) {
  snprintf(b, c, v ? " on" : "off");
  if (v) *col = GREEN;
}
static void vSound(char* b, size_t c, uint16_t* col) { vOnOff(b, c, col, settings().sound); }
static void vBt(char* b, size_t c, uint16_t* col)    { vOnOff(b, c, col, settings().bt); }
static void vWifi(char* b, size_t c, uint16_t* col)  { vOnOff(b, c, col, settings().wifi); }
static void vLed(char* b, size_t c, uint16_t* col)   { vOnOff(b, c, col, settings().led); }
static void vHud(char* b, size_t c, uint16_t* col)   { vOnOff(b, c, col, settings().hud); }
static void vWifiState(char* b, size_t c, uint16_t* col) {
  WifiLinkState ws = wifiLinkState();
  if (ws == WIFI_LINK_ONLINE)       { snprintf(b, c, " up"); *col = GREEN; }
  else if (ws == WIFI_LINK_JOINING) snprintf(b, c, "...");
  else b[0] = 0;
}
static void vClockRot(char* b, size_t c, uint16_t*) {
  static const char* const RN[] = { "auto", "port", "land" };
  snprintf(b, c, "%s", RN[settings().clockRot]);
}
static void vSpecies(char* b, size_t c, uint16_t*) {
  uint8_t total = buddySpeciesCount() + (gifAvailable ? 1 : 0);
  uint8_t pos   = buddyMode ? buddySpeciesIdx() + 1 : total;
  snprintf(b, c, "%u/%u", pos, total);
}

static bool cBright(uint8_t)  { brightLevel = (brightLevel + 1) % 5; applyBrightness(); return true; }
static bool cSound(uint8_t)   { settings().sound = !settings().sound; settingsSave(); return true; }
static bool cBt(uint8_t) {
  // Stored preference only — BLE stays live (tearing down the stack isn't
  // reliable in the Arduino BLE libs).
  settings().bt = !settings().bt; settingsSave(); return true;
}
static bool cWifi(uint8_t) {
  Settings& s = settings();
  s.wifi = !s.wifi;
  if (s.wifi && wifiLinkHasCreds()) wifiLinkConnect();
  else if (!s.wifi) wifiLinkDisconnect();
  settingsSave(); return true;
}
static bool cWifiSetup(uint8_t) {
  wifiLinkStartPortal();
  wifiSetupOpen = true;
  return false;
}
static bool cLed(uint8_t)     { settings().led = !settings().led; settingsSave(); return true; }
static bool cHud(uint8_t)     { settings().hud = !settings().hud; settingsSave(); return true; }
static bool cClockRot(uint8_t){ settings().clockRot = (settings().clockRot + 1) % 3; settingsSave(); return true; }
static bool cPet(uint8_t)     { nextPet(); return true; }
static bool cReset(uint8_t)   { overlayOpen(RESET_OVERLAY); return true; }
static bool cClose(uint8_t)   { return false; }

static const OverlayItem SETTINGS_ITEMS[] = {
  { "brightness", vBright,    cBright,   false },
  { "sound",      vSound,     cSound,    false },
  { "bluetooth",  vBt,        cBt,       false },
  { "wifi",       vWifi,      cWifi,     false },
  { "wifi setup", vWifiState, cWifiSetup,false },
  { "led",        vLed,       cLed,      false },
  { "transcript", vHud,       cHud,      false },
  { "clock rot",  vClockRot,  cClockRot, false },
  { "ascii pet",  vSpecies,   cPet,      false },
  { "reset",      nullptr,    cReset,    false },
  { "back",       nullptr,    cClose,    false },
};
const Overlay SETTINGS_OVERLAY = { SETTINGS_ITEMS, 11, 0, "Next", "Change" };

// -- main menu rows --

static void vDemo(char* b, size_t c, uint16_t*) { snprintf(b, c, dataDemo() ? " on" : "off"); }
static bool cSettings(uint8_t) { overlayOpen(SETTINGS_OVERLAY); return true; }
static bool cPowerOff(uint8_t) { hwPowerOff(); return true; }
static bool cHelp(uint8_t idx) {
  displayMode = DISP_INFO;
  infoSetPage(idx == 2 ? INFO_PG_BUTTONS : INFO_PG_CREDITS);
  applyDisplayMode();
  return false;
}
static bool cDemo(uint8_t) { dataSetDemo(!dataDemo()); return true; }

static const OverlayItem MENU_ITEMS[] = {
  { "settings", nullptr, cSettings, false },
  { "turn off", nullptr, cPowerOff, false },
  { "help",     nullptr, cHelp,     false },
  { "about",    nullptr, cHelp,     false },
  { "demo",     vDemo,   cDemo,     false },
  { "close",    nullptr, cClose,    false },
};
const Overlay MENU_OVERLAY = { MENU_ITEMS, 6, 0, "A", "B" };

// -- reset rows --

static bool cDeleteChar(uint8_t) {
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
  delay(300);
  ESP.restart();
  return false;
}

static bool cForgetWifi(uint8_t) {
  // creds gone, radio off — no reboot needed
  wifiLinkForget();
  return false;
}

static bool cFactory(uint8_t) {
  // factory reset: NVS namespace wipe + filesystem format + BLE bonds.
  // Clears stats, owner, petname, species, settings, wifi creds, GIF
  // characters, and stored LTKs so the next desktop has to re-pair.
  statsFactoryWipe();
  LittleFS.format();
  bleClearBonds();
  delay(300);
  ESP.restart();
  return false;
}

static const OverlayItem RESET_ITEMS[] = {
  { "delete char",   nullptr, cDeleteChar, true  },
  { "forget wifi",   nullptr, cForgetWifi, true  },
  { "factory reset", nullptr, cFactory,    true  },
  { "back",          nullptr, cClose,      false },
};
const Overlay RESET_OVERLAY = { RESET_ITEMS, 4, HOT, "A", "B" };
