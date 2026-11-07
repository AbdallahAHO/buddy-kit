// glance: a hub-fed fleet status panel — the kit's second composition.
// Legos: line-bus + wifi-link + transport-http + transport-ws + agent-state +
// notify-queue + notify-pair + improv-serial + ui-canvas + virtual-display +
// ota. Deliberately no BLE, no faces, no file push. Three full-repaint
// surfaces, topmost first: pairing, notify (an ask pending), status; button
// A drives the first two.
#include <Arduino.h>
#include "hw/hw.h"
#include "wifi_link.h"
#include "ota.h"
#include "virtual_display.h"
#include "glance_state.h"

TamaState tama;
NotifyQueue gNotify;

void setup() {
  // Native USB (HWCDC) delivers a whole JSON line faster than the loop
  // polls, and its default 256-byte RX ring silently drops the tail.
  Serial.setRxBufferSize(2048);
  hwInit();                  // Wire + display + power + input + IMU + RTC + audio
  notifyInit(gNotify);
  glanceCommandsInit();      // ack fan-out (USB + hub + ws) + vdp tee + hub/notify autostart
  wifiLinkInit();
  if (wifiLinkHasCreds()) wifiLinkConnect();
  otaInit(nullptr);          // lib guards a null callback; no progress screen in v0

  Serial.println("glance: ready");
}

void loop() {
  wifiLinkTick();
  // Input first: a press acts on what was drawn last frame, before new lines
  // (a retract, a new head) can change what's under the button.
  hwInputUpdate();
  inputRoute(pairLinkView());
  dataPoll(&tama);           // pump USB (+ Improv) + hub + socket lines into the store
  improvTick();
  pairLinkTick();
  statusTick();              // 1 Hz RTC cache refresh

  PairView pair = pairLinkView();
  bool pairing = pair.phase != PAIR_IDLE;
  notifyScreenTick(!pairing); // expire, seen ack — before the draw shows it
  ledTick(pair);
  if (pairing)                  pairScreenDraw(*hwCanvas(), pair);
  else if (notifyHead(gNotify)) notifyScreenDraw(*hwCanvas());
  else                          statusDraw(*hwCanvas());
  hwDisplayPush();
  virtualDisplayTick();      // tee changed canvas stripes to the vdp client
  // ~20 fps keeps the C6 cool; 50 ms is well inside the 600 ms long press.
  delay(50);
}
