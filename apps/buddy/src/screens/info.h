#pragma once
#include <stdint.h>
#include "ui_canvas.h"
// Info screen, 6 pages. Page index is screen-local state.
const uint8_t INFO_PAGES = 6;
const uint8_t INFO_PG_BUTTONS = 1;
const uint8_t INFO_PG_CREDITS = 5;
void infoDraw(Arduino_GFX& g);
uint8_t infoPageIdx();
void infoSetPage(uint8_t p);
void infoNextPage();   // wraps
