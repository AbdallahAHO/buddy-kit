#include "hw/led.h"
#include "hw/pins.h"
#include <Arduino.h>

#if BOARD_HAS_RGB_LED
void hwLed(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_RGB_LED, r, g, b); }
#else
void hwLed(uint8_t, uint8_t, uint8_t) {}
#endif
