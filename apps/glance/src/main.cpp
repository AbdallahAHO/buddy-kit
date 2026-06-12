// glance: a hub-fed fleet status panel — the kit's second composition.
// Legos: line-bus + wifi-link + transport-http + agent-state + ui-canvas +
// virtual-display + ota. Deliberately no BLE, no faces, no file push, no
// input routing: one screen, repainted every frame.
#include <Arduino.h>
#include "hw/hw.h"
#include "wifi_link.h"
#include "ota.h"
#include "virtual_display.h"
#include "glance_state.h"

TamaState tama;

void setup() {
  // Native USB (HWCDC) delivers a whole JSON line faster than the loop
  // polls, and its default 256-byte RX ring silently drops the tail.
  Serial.setRxBufferSize(2048);
  hwInit();                  // Wire + display + power + input + IMU + RTC + audio
  glanceCommandsInit();      // ack fan-out (USB + hub) + vdp tee + hub autostart
  wifiLinkInit();
  if (wifiLinkHasCreds()) wifiLinkConnect();
  otaInit(nullptr);          // lib guards a null callback; no progress screen in v0

  Serial.println("glance: ready");
}

void loop() {
  wifiLinkTick();
  dataPoll(&tama);           // pump USB + hub lines into the store
  statusTick();              // 1 Hz RTC cache refresh
  statusDraw(*hwCanvas());
  hwDisplayPush();
  virtualDisplayTick();      // tee changed canvas stripes to the vdp client
  // ~20 fps is plenty for a status panel and keeps the C6 cool; no input
  // path means no latency to protect.
  delay(50);
}
