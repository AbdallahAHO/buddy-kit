#pragma once
#include <stdint.h>

// Owns the interaction priority ladder for buttons + touch + gestures:
// approval > wifi setup > overlay (menu/settings/reset) > screen. Emits
// semantic actions (approve/deny, next/confirm, page swipes, pets).
void inputRoute(uint32_t now, bool inPrompt);

// True while a recent pet interaction should keep the buddy awake.
bool inputPlayful(uint32_t now);
