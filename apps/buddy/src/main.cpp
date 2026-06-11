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
#include "overlays.h"
#include "input_router.h"
#include "hid_mouse.h"
#include "jiggler.h"
#include "ota.h"
#include "screens/ota_screen.h"
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
uint8_t brightLevel = 4;           // 0..4 → ScreenBreath 20..100

uint8_t displayMode = DISP_NORMAL;
char     lastPromptId[40] = "";
uint32_t lastInteractMs = 0;
bool     dimmed = false;
bool     screenOff = false;
bool     buddyMode = false;
bool     gifAvailable = false;
const uint8_t SPECIES_GIF = 0xFF;   // species NVS sentinel: use the installed GIF

// Cycle GIF (if installed) → ASCII species 0..N-1 → GIF. Persisted to the
// existing "species" NVS key; 0xFF means GIF mode.
void nextPet() {
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

void prevPet() {
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

void applyBrightness() { hwDisplayBrightness(brightLevel); }

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

void beep(uint16_t freq, uint16_t dur) {
  if (settings().sound) hwBeep(freq, dur);
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

bool    wifiSetupOpen = false;   // QR pairing screen (drawWifiSetup)
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
  hidMouseInit();            // HID mouse service on the same GATT server
  otaInit(otaScreenOnProgress);
  appCommandsInit();         // ack fan-out (USB+BLE) + file-push sink
  wifiLinkInit();
  applyBrightness();
  lastInteractMs = millis();
  statsLoad();
  settingsLoad();
  if (settings().wifi && wifiLinkHasCreds()) wifiLinkConnect();
  jigglerApply(settings().jiggler);
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
  jigglerTick(millis());
  // Wi-Fi resilience: a failed boot join (router rebooting, out of range)
  // previously parked the radio OFF forever. Retry every 5 min while the
  // setting is on and creds exist.
  {
    static uint32_t _wifiRetryAt = 0;
    if (settings().wifi && wifiLinkHasCreds() && wifiLinkState() == WIFI_LINK_OFF) {
      if ((int32_t)(millis() - _wifiRetryAt) >= 0) {
        _wifiRetryAt = millis() + 5UL * 60UL * 1000UL;
        wifiLinkConnect();
      }
    } else {
      _wifiRetryAt = millis();   // armed: first retry fires 0s after entering OFF
    }
  }
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
    if (!overlayActive() && !screenOff && checkShake() && (int32_t)(now - oneShotUntil) >= 0) {
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
      overlayClose();
      applyDisplayMode();
      characterInvalidate();
      if (buddyMode) buddyInvalidate();
    }
  }

  bool inPrompt = tama.promptId[0] && !responseSent;
  inputRoute(now, inPrompt);

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
               && !overlayActive() && !inPrompt
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
    if (inputPlayful(now)) {
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
  if (!pk && lastPasskey) {   // passkey dismissed — it painted the full canvas
    applyDisplayMode();
    if (buddyMode) buddyInvalidate();
  }
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
    if (otaScreenActive()) otaScreenDraw(spr);
    else if (blePasskey()) passkeyDraw(spr);
    else if (wifiSetupOpen) wifiSetupDraw(spr);
    else if (clocking) homeClockDraw(spr);
    else if (displayMode == DISP_INFO) infoDraw(spr);
    else if (displayMode == DISP_PET) petDraw(spr);
    else if (settings().hud) homeHudDraw(spr);
    overlayDraw(spr);
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
          || inPrompt || overlayActive()
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
