#pragma once
#include <stdint.h>
#include "ui_canvas.h"
// Home screen pieces: transcript HUD (with the approval card when a prompt
// is pending) and the idle clock face. Scroll + RTC cache are screen-local.
void homeHudDraw(Arduino_GFX& g);
void homeClockDraw(Arduino_GFX& g);
void homeClockTick();        // 1 Hz RTC re-read; also caches USB presence
uint8_t homeClockHour();     // for the time-of-day mood logic
bool homeOnUsb();            // cached USB presence (1 Hz, from homeClockTick)
void homeBumpScroll();       // transcript scroll-back (BtnB / bottom strip)
