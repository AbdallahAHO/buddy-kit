#pragma once
#include <stdint.h>
#include "ui_canvas.h"
// Pet screen: stats page + how-to page. Page index is screen-local state.
const uint8_t PET_PAGES = 2;
void petDraw(Arduino_GFX& g);
uint8_t petPageIdx();
void petSetPage(uint8_t p);
void petNextPage();   // wraps
