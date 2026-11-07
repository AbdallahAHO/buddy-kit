#pragma once
#include <stdint.h>

/** hwLed sets the board's RGB status LED; a no-op on boards without one. */
void hwLed(uint8_t r, uint8_t g, uint8_t b);
