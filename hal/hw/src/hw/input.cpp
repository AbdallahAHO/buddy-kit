#include "hw/input.h"
#include "hw/pins.h"
#include "hw/expander.h"
#include "hw/power.h"
#include <Arduino.h>

#if BOARD_TOUCH_CST92XX
  #include <Wire.h>
  #include "TouchDrvCSTXXX.hpp"
#elif BOARD_TOUCH_CST816
  #include <Wire.h>
#else
  #include <Arduino_DriveBus_Library.h>
#endif

static HwBtn   s_a, s_b;
static HwTouch s_tp;
static uint8_t s_axpEvt = 0;

#if BOARD_TOUCH_CST92XX
static TouchDrvCST92xx s_cst;
#elif BOARD_TOUCH_CST816
// CST816D polled over its own I2C bus. Register map per the xiaozhi board
// code (the managed esp_lcd_touch_cst816s component targets a sibling chip).
static TwoWire s_tpBus(1);
static bool    s_tpPresent = false;   // false on the non-touch SKU
static constexpr uint8_t CST816_ADDR = 0x15;
#else
static std::shared_ptr<Arduino_IIC_DriveBus> s_iicBus;
static std::unique_ptr<Arduino_IIC>          s_ft3168;
#endif
static volatile bool                          s_tpIrqFlag = false;

static void IRAM_ATTR onTouchIrq() { s_tpIrqFlag = true; }

bool HwBtn::pressedFor(uint32_t ms) {
  return isPressed && (millis() - pressedAt) >= ms;
}

bool hwInputInit() {
  pinMode(PIN_KEY1, INPUT_PULLUP);   // GPIO0 has external pullup; INPUT_PULLUP is harmless
#if BOARD_HAS_KEY2
  pinMode(PIN_KEY2, INPUT_PULLUP);   // External R18 10K already pulls high; INPUT_PULLUP is harmless
#endif
#if BOARD_BTN_THIRD
  pinMode(PIN_KEY_BOOT, INPUT_PULLUP);   // External R8 10K already pulls high; INPUT_PULLUP is harmless
#endif

#if BOARD_TOUCH_CST92XX
  // CST92xx @ 0x5A via SensorLib. Reset is handled by hwExpanderResetSequence()
  // (TP_RST is shared with LCD_RESET on 1.75C), so pass rstPin=-1 to skip the
  // driver's internal reset — otherwise it would also re-reset the display.
  s_cst.setPins(-1, PIN_TP_INT);
  if (!s_cst.begin(Wire, 0x5A, PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("hwInput: CST92xx init failed");
    return false;
  }
  Serial.printf("hwInput: CST92xx model=%s\n", s_cst.getModelName());
  s_cst.setMaxCoordinates(LCD_W_PHYS, LCD_H_PHYS);
  s_cst.setMirrorXY(true, true);
  pinMode(PIN_TP_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_TP_INT), onTouchIrq, FALLING);
  return true;
#elif BOARD_TOUCH_CST816
  // hwExpanderResetSequence() already pulsed TP_RESET; the chip needs ~50 ms
  // after reset before it answers on I2C.
  delay(50);
  if (!s_tpBus.begin(PIN_TP_SDA, PIN_TP_SCL, 400000)) {
    Serial.println("hwInput: touch I2C begin failed; button-only");
    return true;
  }
  // Retry for ~500 ms: the controller's own boot after reset varies.
  uint8_t chipId = 0xFF;
  for (int i = 0; i < 10 && (chipId == 0xFF || chipId == 0x00); i++) {
    if (i) delay(50);
    s_tpBus.beginTransmission(CST816_ADDR);
    s_tpBus.write(0xA3);   // chip-id register (as read by the xiaozhi board code)
    if (s_tpBus.endTransmission(false) == 0 &&
        s_tpBus.requestFrom(CST816_ADDR, (uint8_t)1) == 1) {
      chipId = s_tpBus.read();
    }
  }
  Serial.printf("hwInput: CST816 chip id 0x%02X\n", chipId);
  // 0xFF / 0x00 = nothing on the bus: the non-touch SKU. Not fatal — the
  // board still has the BOOT key.
  s_tpPresent = chipId != 0xFF && chipId != 0x00;
  if (!s_tpPresent) {
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      s_tpBus.beginTransmission(a);
      if (s_tpBus.endTransmission() == 0) { Serial.printf("hwInput: touch bus has 0x%02X\n", a); found++; }
    }
    Serial.printf("hwInput: no touch panel (%d devices on touch bus); button-only\n", found);
    return true;
  }
  // Disable auto-sleep: a sleeping CST816 NAKs I2C until touched.
  s_tpBus.beginTransmission(CST816_ADDR);
  s_tpBus.write(0xFE);
  s_tpBus.write(0x01);
  s_tpBus.endTransmission();
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  return true;
#else
  s_iicBus = std::make_shared<Arduino_HWIIC>(PIN_I2C_SDA, PIN_I2C_SCL, &Wire);
  s_ft3168.reset(new Arduino_FT3x68(s_iicBus, FT3168_DEVICE_ADDRESS,
                                    DRIVEBUS_DEFAULT_VALUE, PIN_TP_INT, onTouchIrq));
  for (int i = 0; i < 5; i++) {
    if (s_ft3168->begin()) return true;
    delay(100);
  }
  Serial.println("hwInput: FT3168 init failed");
  return false;
#endif
}

static void scanKey1() {
  uint32_t now = millis();
#if BOARD_KEY1_ACTIVE_HIGH
  bool pressed = digitalRead(PIN_KEY1) == HIGH;
#else
  bool pressed = digitalRead(PIN_KEY1) == LOW;
#endif
  s_a.wasPressed  = pressed && !s_a.isPressed;
  s_a.wasReleased = !pressed && s_a.isPressed;
  if (s_a.wasPressed) s_a.pressedAt = now;
  s_a.isPressed = pressed;
}

#if BOARD_HAS_KEY2
static void scanKey2() {
  uint32_t now = millis();
  bool pressed = digitalRead(PIN_KEY2) == LOW;
  s_b.wasPressed  = pressed && !s_b.isPressed;
  s_b.wasReleased = !pressed && s_b.isPressed;
  if (s_b.wasPressed) s_b.pressedAt = now;
  s_b.isPressed = pressed;
}
#endif

#if BOARD_BTN_THIRD
// BOOT key (GPIO9 on 2.16) acts as a menu shortcut: a short tap synthesises
// BTN_A_LONG_PRESS, which main.cpp's existing handler treats as "open menu".
// main.cpp itself is unchanged.
static uint32_t s_bootPressedAt = 0;
static void scanBootKey() {
  bool pressed = digitalRead(PIN_KEY_BOOT) == LOW;
  if (pressed && !s_bootPressedAt) {
    s_bootPressedAt = millis();
  } else if (!pressed && s_bootPressedAt) {
    uint32_t held = millis() - s_bootPressedAt;
    s_bootPressedAt = 0;
    if (held > 30 && held < 1000) {
      // Synthesise a long-press of A so the menu opens.
      s_a.wasPressed  = true;
      s_a.wasReleased = true;
      s_a.pressedAt   = millis() - 1500;  // > LONG_PRESS_MS so isLongPress check passes
      s_a.isPressed   = false;
    }
  }
}
#endif

#if !BOARD_HAS_KEY2 && BOARD_HAS_AXP2101
static void scanAxp() {
  if (hwExpanderAxpIrqLow()) {
    if (hwAxpPekeyShortPress()) s_axpEvt = 0x02;
    if (hwAxpPekeyLongPress())  s_axpEvt = 0x04;
  }
  // Route 0x02 to BtnB pulse for one frame:
  bool pressed = (s_axpEvt == 0x02);
  s_b.wasPressed  = pressed;
  s_b.wasReleased = pressed;
  s_b.isPressed   = false;
  if (pressed) s_axpEvt = 0;
  // 0x04 stays in s_axpEvt until consumed by hwAxpBtnEvent()
}
#endif

static void scanTouch() {
  // Poll when IRQ fires OR when a finger was down last frame — both FT3168
  // and CST92xx only reliably IRQ on state edges, so a drag wouldn't advance
  // x/y without this.
#if BOARD_TOUCH_CST816
  // No IRQ hookup: a 6-byte read every frame is ~0.2 ms at 400 kHz.
  bool shouldPoll = s_tpPresent;
#else
  bool shouldPoll = s_tpIrqFlag || s_tp.down;
#endif
  s_tpIrqFlag = false;

  if (!shouldPoll) {
    s_tp.justPressed  = false;
    s_tp.justReleased = false;
    return;
  }

#if BOARD_TOUCH_CST92XX
  int16_t x[2] = {0}, y[2] = {0};
  uint8_t n = s_cst.getPoint(x, y, s_cst.getSupportTouchPoint());
  if (n > 0) {
    s_tp.justPressed  = !s_tp.down;
    s_tp.justReleased = false;
    // Mirror of hwDisplayPush's letterbox scale: reverse (physical → canvas).
    // Use BOARD_* (raw macros from board header) since display.h's constexpr
    // wrappers aren't visible here.
    #if BOARD_DISPLAY_LETTERBOX
      constexpr int OFF_X  = (LCD_W_PHYS - BOARD_DISPLAY_DEST_W) / 2;
      constexpr int OFF_Y  = (LCD_H_PHYS - BOARD_DISPLAY_DEST_H) / 2;
      int dx = x[0] - OFF_X;
      int dy = y[0] - OFF_Y;
      int tx = (dx * BOARD_HW_W) / BOARD_DISPLAY_DEST_W;
      int ty = (dy * BOARD_HW_H) / BOARD_DISPLAY_DEST_H;
      if (tx < 0) tx = 0; else if (tx >= BOARD_HW_W) tx = BOARD_HW_W - 1;
      if (ty < 0) ty = 0; else if (ty >= BOARD_HW_H) ty = BOARD_HW_H - 1;
      s_tp.x = tx;
      s_tp.y = ty;
    #else
      // Non-letterbox: physical → canvas via OFFSET subtract + scale downscale.
      // OFFSET is 0 on 1.8 (full-fill), 148/128 on 2.16 (centred 184×224 in 480×480).
      int dx = x[0] - BOARD_DISPLAY_OFFSET_X;
      int dy = y[0] - BOARD_DISPLAY_OFFSET_Y;
      int tx = dx / BOARD_DISPLAY_SCALE;
      int ty = dy / BOARD_DISPLAY_SCALE;
      if (tx < 0) tx = 0; else if (tx >= BOARD_HW_W) tx = BOARD_HW_W - 1;
      if (ty < 0) ty = 0; else if (ty >= BOARD_HW_H) ty = BOARD_HW_H - 1;
      s_tp.x = tx;
      s_tp.y = ty;
    #endif
    s_tp.down = true;
  } else {
    s_tp.justReleased = s_tp.down;
    s_tp.down = false;
    s_tp.justPressed  = false;
  }
#elif BOARD_TOUCH_CST816
  uint8_t buf[6] = {0};
  s_tpBus.beginTransmission(CST816_ADDR);
  s_tpBus.write(0x02);   // finger count, then X/Y high+low bytes
  bool ok = s_tpBus.endTransmission(false) == 0 &&
            s_tpBus.requestFrom(CST816_ADDR, (uint8_t)6) == 6;
  if (ok) for (int i = 0; i < 6; i++) buf[i] = s_tpBus.read();
  uint8_t fingers = ok ? (buf[0] & 0x0F) : 0;
  if (fingers > 0) {
    // Panel coords are canvas coords (240×240, scale 1, no offset).
    int tx = ((buf[1] & 0x0F) << 8) | buf[2];
    int ty = ((buf[3] & 0x0F) << 8) | buf[4];
    if (tx >= BOARD_HW_W) tx = BOARD_HW_W - 1;
    if (ty >= BOARD_HW_H) ty = BOARD_HW_H - 1;
    s_tp.justPressed  = !s_tp.down;
    s_tp.justReleased = false;
    s_tp.x = tx;
    s_tp.y = ty;
    s_tp.down = true;
  } else {
    s_tp.justReleased = s_tp.down;
    s_tp.down = false;
    s_tp.justPressed  = false;
  }
#else
  uint8_t fingers = (uint8_t)s_ft3168->IIC_Read_Device_Value(
      Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  if (fingers > 0) {
    int rx = (int)s_ft3168->IIC_Read_Device_Value(
        Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    int ry = (int)s_ft3168->IIC_Read_Device_Value(
        Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
    s_tp.justPressed  = !s_tp.down;
    s_tp.justReleased = false;
    int dx = rx - BOARD_DISPLAY_OFFSET_X;
    int dy = ry - BOARD_DISPLAY_OFFSET_Y;
    int tx = dx / BOARD_DISPLAY_SCALE;
    int ty = dy / BOARD_DISPLAY_SCALE;
    if (tx < 0) tx = 0; else if (tx >= BOARD_HW_W) tx = BOARD_HW_W - 1;
    if (ty < 0) ty = 0; else if (ty >= BOARD_HW_H) ty = BOARD_HW_H - 1;
    s_tp.x = tx;
    s_tp.y = ty;
    s_tp.down = true;
  } else {
    s_tp.justReleased = s_tp.down;
    s_tp.down = false;
    s_tp.justPressed  = false;
  }
#endif
}

void hwInputUpdate() {
  scanKey1();
#if BOARD_HAS_KEY2
  scanKey2();
#elif BOARD_HAS_AXP2101
  scanAxp();
#endif
#if BOARD_BTN_THIRD
  scanBootKey();
#endif
  scanTouch();
}

#if BOARD_BTN_SWAP_AB
HwBtn& hwBtnA() { return s_b; }
HwBtn& hwBtnB() { return s_a; }
#else
HwBtn& hwBtnA() { return s_a; }
HwBtn& hwBtnB() { return s_b; }
#endif

uint8_t hwAxpBtnEvent() {
  uint8_t e = s_axpEvt;
  if (e == 0x04) s_axpEvt = 0;   // 0x02 already cleared by scanAxp
  return e;
}

const HwTouch& hwTouch() { return s_tp; }
bool hwTouchIrqPending() { return s_tpIrqFlag; }
