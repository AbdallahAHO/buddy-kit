#include "info.h"
#include <Arduino.h>
#include "../app_state.h"
#include "ui_canvas.h"
#include <esp_mac.h>
#include "character.h"
#include "buddy.h"
#include "ble_bridge.h"
#include "hw/hw.h"
#include "stats.h"
#include "agent_link.h"
#include "transport_http.h"
#include "wifi_link.h"

// Persistent screen-level title row ("INFO  n/3") matching the PET header,
// then a per-page section label below it. The fixed title is the cue that
// B cycles pages here just like it does on PET.
static uint8_t infoPage = 0;

static void _infoHeader(Arduino_GFX& g, const Palette& p, int& y, const char* section, uint8_t page) {
  g.setTextColor(p.text, p.bg);
  g.setCursor(SAFE_L, y); g.print("Info");
  g.setTextColor(p.textDim, p.bg);
  g.setCursor(SAFE_R - 24, y); g.printf("%u/%u", page + 1, INFO_PAGES);
  y += 12;
  g.setTextColor(p.body, p.bg);
  g.setCursor(SAFE_L, y); g.print(section);
  y += 12;
}

void infoDraw(Arduino_GFX& g) {
  const Palette& p = characterPalette();
  const int TOP = 70;
  g.fillRect(0, TOP, W, H - TOP, p.bg);
  g.setTextSize(1);
  int y = TOP + 2;
  auto ln = [&](const char* fmt, ...) {
    char b[32]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
    g.setCursor(SAFE_L, y); g.print(b); y += 8;
  };

  if (infoPage == 0) {
    _infoHeader(g, p, y, "ABOUT", infoPage);
    g.setTextColor(p.textDim, p.bg);
    ln("I watch your Claude");
    ln("desktop sessions.");
    y += 6;
    ln("I sleep when nothing's");
    ln("happening, wake when");
    ln("you start working,");
    ln("get impatient when");
    ln("approvals pile up.");
    y += 6;
    g.setTextColor(p.text, p.bg);
    ln("Press A on a prompt");
    ln("to approve from here.");
    y += 6;
    g.setTextColor(p.textDim, p.bg);
    ln("18 species. Settings");
    ln("> ascii pet to cycle.");

  } else if (infoPage == 1) {
    _infoHeader(g, p, y, "BUTTONS", infoPage);
    g.setTextColor(p.text, p.bg);    ln("A   front");
    g.setTextColor(p.textDim, p.bg); ln("    next screen");
    ln("    approve prompt"); y += 4;
    g.setTextColor(p.text, p.bg);    ln("B   right side");
    g.setTextColor(p.textDim, p.bg); ln("    next page");
    ln("    deny prompt"); y += 4;
    g.setTextColor(p.text, p.bg);    ln("hold A");
    g.setTextColor(p.textDim, p.bg); ln("    menu"); y += 4;
    g.setTextColor(p.text, p.bg);    ln("Power  left side");
    g.setTextColor(p.textDim, p.bg); ln("    tap = screen off");
    ln("    hold 6s = off");

  } else if (infoPage == 2) {
    _infoHeader(g, p, y, "CLAUDE", infoPage);
    g.setTextColor(p.textDim, p.bg);
    ln("  sessions  %u", tama.sessionsTotal);
    ln("  running   %u", tama.sessionsRunning);
    ln("  waiting   %u", tama.sessionsWaiting);
    y += 8;
    g.setTextColor(p.text, p.bg);
    ln("LINK");
    g.setTextColor(p.textDim, p.bg);
    ln("  via       %s", dataScenarioName());
    ln("  ble       %s", !bleConnected() ? "-" : bleSecure() ? "encrypted" : "OPEN");
    uint32_t age = (millis() - tama.lastUpdated) / 1000;
    ln("  last msg  %lus", (unsigned long)age);
    ln("  state     %s", stateNames[activeState]);

  } else if (infoPage == 3) {
    _infoHeader(g, p, y, "DEVICE", infoPage);

    HwBattery hb = hwBattery();
    int vBat_mV  = hb.mV;
    int iBat_mA  = hb.mA;       // always 0 on AXP2101 (current not exposed)
    int vBus_mV  = hb.usbPresent ? 5000 : 0;
    int pct      = hb.pct;
    bool usb     = hb.usbPresent;
    bool charging = hb.charging;
    bool full    = usb && vBat_mV > 4100 && !charging;

    g.setTextColor(p.text, p.bg);
    g.setTextSize(2);
    g.setCursor(SAFE_L, y);
    g.printf("%d%%", pct);
    g.setTextSize(1);
    g.setTextColor(full ? GREEN : (charging ? HOT : p.textDim), p.bg);
    g.setCursor(60, y + 4);
    g.print(full ? "full" : (charging ? "charging" : (usb ? "usb" : "battery")));
    y += 20;

    g.setTextColor(p.textDim, p.bg);
    ln("  battery  %d.%02dV", vBat_mV/1000, (vBat_mV%1000)/10);
    ln("  current  %+dmA", iBat_mA);
    if (usb) ln("  usb in   %d.%02dV", vBus_mV/1000, (vBus_mV%1000)/10);
    y += 8;

    g.setTextColor(p.text, p.bg);
    ln("SYSTEM");
    g.setTextColor(p.textDim, p.bg);
    if (ownerName()[0]) ln("  owner    %s", ownerName());
    uint32_t up = millis() / 1000;
    ln("  uptime   %luh %02lum", up / 3600, (up / 60) % 60);
    ln("  heap     %uKB", ESP.getFreeHeap() / 1024);
    ln("  bright   %u/4", brightLevel);
    ln("  bt       %s", settings().bt ? (dataBtActive() ? "linked" : "on") : "off");
    ln("  temp     %dC", (int)hwBattery().tempC);

  } else if (infoPage == 4) {
    _infoHeader(g, p, y, "BLUETOOTH", infoPage);
    bool linked = settings().bt && dataBtActive();

    g.setTextColor(linked ? GREEN : (settings().bt ? HOT : p.textDim), p.bg);
    ln("%s", linked ? "linked" : (settings().bt ? "discover" : "off"));
    y += 4;

    g.setTextColor(p.text, p.bg);
    ln("%s", btName);
    g.setTextColor(p.textDim, p.bg);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    ln("%02X:%02X:%02X:%02X:%02X:%02X",
       mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    y += 4;

    if (linked) {
      uint32_t age = (millis() - tama.lastUpdated) / 1000;
      ln("last msg  %lus", (unsigned long)age);
    } else if (settings().bt) {
      g.setTextColor(p.text, p.bg);
      ln("TO PAIR");
      g.setTextColor(p.textDim, p.bg);
      ln("Open Claude desktop");
      ln("> Developer");
      ln("> Hardware Buddy");
      y += 4;
      ln("auto-connects via BLE");
    }

  } else {
    _infoHeader(g, p, y, "CREDITS", infoPage);
    g.setTextColor(p.textDim, p.bg);
    ln("made by");
    y += 4;
    g.setTextColor(p.text, p.bg);
    ln("Felix Rieseberg");
    y += 12;
    g.setTextColor(p.textDim, p.bg);
    ln("hardware adaptation");
    y += 4;
    g.setTextColor(p.text, p.bg);
    ln("yadong");
    y += 12;
    g.setTextColor(p.textDim, p.bg);
    ln("hardware");
    y += 4;
    g.setTextColor(p.text, p.bg);
    ln(BOARD_MODEL_LINE1);
    ln(BOARD_MODEL_LINE2);
  }
}


// Greedy word-wrap into fixed-width rows. Continuation rows get a leading
// space. Returns number of rows written.
// UTF-8 continuation byte = 0b10xxxxxx. Pull `take` back so we never
// land mid-codepoint when hard-breaking long Chinese sentences.


uint8_t infoPageIdx() { return infoPage; }
void infoSetPage(uint8_t p) { infoPage = p % INFO_PAGES; }
void infoNextPage() { infoPage = (infoPage + 1) % INFO_PAGES; }
