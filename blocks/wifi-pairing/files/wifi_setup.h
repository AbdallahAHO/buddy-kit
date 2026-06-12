#pragma once
#include "ui_canvas.h"
// QR pairing screen: WIFI: QR + AP creds + live join status.
void wifiSetupDraw(Arduino_GFX& g);
void wifiSetupClose();   // stop portal + return to normal rendering
