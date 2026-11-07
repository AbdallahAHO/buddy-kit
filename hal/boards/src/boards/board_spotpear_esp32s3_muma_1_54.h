// src/boards/board_spotpear_esp32s3_muma_1_54.h
#pragma once

// Spotpear ESP32-S3-1.54-MUMA (sold as a xiaozhi AI chatbot board).
// Display: ST7789 240×240 square IPS over plain 4-wire SPI, backlight on an
// inverted PWM pin. Canvas = panel, 1:1, so no upscale and no offset.
// No PMU (no AXP2101), no IMU, no RTC chip, no I/O expander.
// Pins from the board's xiaozhi config.h, proven by focus-buddy on this board.
#define LCD_W_PHYS              240
#define LCD_H_PHYS              240
#define BOARD_HW_W              240
#define BOARD_HW_H              240
#define BOARD_SAFE_INSET        4
#define BOARD_DISPLAY_OFFSET_X  0
#define BOARD_DISPLAY_OFFSET_Y  0
#define BOARD_DISPLAY_SCALE     1

// SPI to ST7789
#define PIN_LCD_SCLK   4
#define PIN_LCD_MOSI   2
#define PIN_LCD_CS     5
#define PIN_LCD_DC     47
#define PIN_LCD_RESET  38
#define PIN_LCD_BL     42   // PWM, inverted: duty 0 = full bright, 255 = off

// I2C bus 0 (ES8311 codec only)
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   14

// Touch: CST816D on its own I2C bus (TwoWire 1). Absent on the non-touch SKU.
#define PIN_TP_SDA    11
#define PIN_TP_SCL    7
#define PIN_TP_RESET  6
#define PIN_TP_INT    12

// I2S to ES8311 codec (same wiring as the Waveshare 1.8)
#define PIN_I2S_MCLK  16
#define PIN_I2S_BCLK  9
#define PIN_I2S_WS    45
#define PIN_I2S_DI    10
#define PIN_I2S_DO    8
#define PIN_PA_CTRL   46

// Buttons: BOOT is the only GPIO key (PWR is a hardware power latch).
#define PIN_KEY1      0    // BOOT, active-low

// WS2812 RGB LED
#define PIN_RGB_LED   48

// Battery sense (1:2 divider on ADC1_CH0) + charge detect. Fine to read with
// no cell fitted — usbPresent stays true on a USB-only desk build.
#define PIN_BAT_ADC     1
#define PIN_CHARGE_DET  41

// Capability flags
#define BOARD_HAS_PSRAM            1
#define BOARD_HAS_TCA9554          0
#define BOARD_HAS_PCF85063         0
#define BOARD_HAS_PA_CTRL          1
#define BOARD_HAS_AXP2101          0
#define BOARD_HAS_QMI8658          0
#define BOARD_HAS_RGB_LED          1
#define BOARD_LCD_RST_VIA_PMU      0
#define BOARD_AXP_PWRON_4S_OFF     0
#define BOARD_DISPLAY_ST7789       1
#define BOARD_DISPLAY_CO5300       0
#define BOARD_DISPLAY_LETTERBOX    0
#define BOARD_DISPLAY_PUSH_STREAMED  0
#define BOARD_TOUCH_CST92XX        0
#define BOARD_TOUCH_CST816         1
#define BOARD_BTN_SWAP_AB          0
#define BOARD_BTN_THIRD            0
#define BOARD_KEY1_ACTIVE_HIGH     0
#define BOARD_HAS_KEY2             0

// Credits page
#define BOARD_MODEL_LINE1  "Spotpear ESP32-S3"
#define BOARD_MODEL_LINE2  "Muma 1.54"

#define BOARD_DISPLAY_SH8601_VENDOR_INIT  0
#define BOARD_AXP_ENABLE_AUX_LDOS  0
#define BOARD_CO5300_COL_OFFSET    0
#define BOARD_DISPLAY_ROTATION     0
#define BOARD_CO5300_MADCTL        0
