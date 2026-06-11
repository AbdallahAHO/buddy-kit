#pragma once
#include "ui_canvas.h"
#include "ota.h"
// Full-screen OTA progress (download bar / applying / failed). Shown while
// otaScreenActive(); the device reboots itself on success.
void otaScreenDraw(Arduino_GFX& g);
bool otaScreenActive();
void otaScreenOnProgress(const OtaProgress& p);   // wired to otaInit's callback
