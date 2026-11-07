// src/hw/pins.h
#pragma once

#if defined(BOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_8)
  #include "boards/board_waveshare_esp32s3_touch_amoled_1_8.h"
#elif defined(BOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_75C)
  #include "boards/board_waveshare_esp32s3_touch_amoled_1_75c.h"
#elif defined(BOARD_WAVESHARE_ESP32C6_TOUCH_AMOLED_2_16)
  #include "boards/board_waveshare_esp32c6_touch_amoled_2_16.h"
#elif defined(BOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_2_16)
  #include "boards/board_waveshare_esp32s3_touch_amoled_2_16.h"
#elif defined(BOARD_SPOTPEAR_ESP32S3_MUMA_1_54)
  #include "boards/board_spotpear_esp32s3_muma_1_54.h"
#else
  #error "No board defined. Add -DBOARD_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_8, _1_75C, _ESP32C6_TOUCH_AMOLED_2_16, _ESP32S3_TOUCH_AMOLED_2_16, or -DBOARD_SPOTPEAR_ESP32S3_MUMA_1_54 to build_flags in platformio.ini."
#endif

// Flags added with the Muma port. The Waveshare AMOLED headers predate them,
// so they default to the AMOLED behavior here instead of in every header.
#ifndef BOARD_HAS_QMI8658
  #define BOARD_HAS_QMI8658     1
#endif
#ifndef BOARD_HAS_RGB_LED
  #define BOARD_HAS_RGB_LED     0
#endif
#ifndef BOARD_DISPLAY_ST7789
  #define BOARD_DISPLAY_ST7789  0
#endif
#ifndef BOARD_TOUCH_CST816
  #define BOARD_TOUCH_CST816    0
#endif
