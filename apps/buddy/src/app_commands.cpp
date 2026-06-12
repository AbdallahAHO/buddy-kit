// Buddy-app command handlers + storage policy for the file-push protocol.
#include "app_commands.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "line_bus.h"
#include "file_push.h"
#include "transport_usb.h"
#include "transport_ble.h"
#include "wifi_link.h"
#include "transport_http.h"
#include <Preferences.h>
#include "stats.h"
#include "app_state.h"
#include "character.h"
#include "buddy.h"
#include "ble_bridge.h"
#include "hw/hw.h"
#include "hid_mouse.h"
#include "jiggler.h"
#include "ota.h"
#include "virtual_display.h"
#include <esp_ota_ops.h>

LineOut gLineOut;

// vdp frames go to USB only: stripe lines run ~1 KB, which BLE NUS would
// fragment and the hub's 1 Hz POST would flood. Acks still fan out.
static LineOut vdpOut;

// ---------------------------------------------------------------------------
// FileSink: LittleFS under /characters/, wipe-all-on-begin policy. Bodies
// ported verbatim from the original xfer.h.
// ---------------------------------------------------------------------------

static File _sinkFile;

static uint32_t _wipeDir(const char* dir) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) { LittleFS.mkdir(dir); return 0; }
  uint32_t freed = 0;
  File f = d.openNextFile();
  while (f) {
    freed += f.size();
    char p[80];
    snprintf(p, sizeof(p), "%s/%s", dir, f.name());
    f.close();
    LittleFS.remove(p);
    f = d.openNextFile();
  }
  d.close();
  return freed;
}

// Only one character lives on the device at a time. Installing a new one
// under a different name would otherwise leave the old one's files eating
// space. Wipe everything under /characters/, return total bytes reclaimed.
static uint32_t _wipeAllChars() {
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) { LittleFS.mkdir("/characters"); return 0; }
  uint32_t freed = 0;
  File sub = root.openNextFile();
  while (sub) {
    if (sub.isDirectory()) {
      char p[64];
      snprintf(p, sizeof(p), "/characters/%s", sub.name());
      sub.close();
      freed += _wipeDir(p);
      LittleFS.rmdir(p);
    } else {
      sub.close();
    }
    sub = root.openNextFile();
  }
  root.close();
  return freed;
}

static bool _sinkBegin(void*, const char* setName, uint32_t totalBytes,
                       uint32_t* availOut, char* err, size_t errLen) {
  // Fit check: free space after wiping everything under /characters/.
  // Do the math before touching the filesystem so a failed check leaves
  // the current character intact.
  uint32_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
  uint32_t reclaimable = 0;
  {
    File r = LittleFS.open("/characters");
    if (r && r.isDirectory()) {
      File s = r.openNextFile();
      while (s) {
        if (s.isDirectory()) {
          File f = s.openNextFile();
          while (f) { reclaimable += f.size(); f.close(); f = s.openNextFile(); }
        }
        s.close(); s = r.openNextFile();
      }
      r.close();
    }
  }
  // Headroom for LittleFS metadata overhead — it's not byte-for-byte.
  uint32_t available = free + reclaimable;
  if (totalBytes > 0 && totalBytes + 4096 > available) {
    *availOut = available;
    snprintf(err, errLen, "need %luK, have %luK",
             (unsigned long)(totalBytes/1024), (unsigned long)(available/1024));
    return false;
  }
  characterClose();
  _wipeAllChars();
  char dir[48]; snprintf(dir, sizeof(dir), "/characters/%s", setName);
  LittleFS.mkdir(dir);
  return true;
}

static char _sinkSetDir[24] = "pet";

static bool _sinkOpen(void*, const char* relPath) {
  char full[80];
  snprintf(full, sizeof(full), "/characters/%s/%s", _sinkSetDir, relPath);
  _sinkFile = LittleFS.open(full, "w");
  return (bool)_sinkFile;
}

static bool _sinkWrite(void*, const uint8_t* d, size_t n) {
  _sinkFile.write(d, n);
  return true;
}

static void _sinkClose(void*) { _sinkFile.close(); }

static bool _sinkFinish(void*, const char* setName) {
  bool ok = characterInit(setName);
  if (ok) { buddyMode = false; gifAvailable = true; speciesIdxSave(0xFF); }
  return ok;
}

static const FileSink APP_FILE_SINK = {
  // begin also records the set dir for subsequent opens
  [](void* u, const char* name, uint32_t total, uint32_t* avail, char* err, size_t errLen) {
    strncpy(_sinkSetDir, name, sizeof(_sinkSetDir)-1); _sinkSetDir[sizeof(_sinkSetDir)-1] = 0;
    return _sinkBegin(u, name, total, avail, err, errLen);
  },
  _sinkOpen, _sinkWrite, _sinkClose, _sinkFinish, nullptr
};

// ---------------------------------------------------------------------------
// Command handlers (bodies ported verbatim from xfer.h)
// ---------------------------------------------------------------------------

static bool cmdName(JsonDocument& doc, void*) {
  const char* n = doc["name"];
  if (n) petNameSet(n);
  lineBusAck(gLineOut, "name", n != nullptr);
  return true;
}

static bool cmdSpecies(JsonDocument& doc, void*) {
  uint8_t idx = doc["idx"] | 0xFF;
  speciesIdxSave(idx);
  buddyMode = !(gifAvailable && idx == 0xFF);
  if (buddyMode) buddySetSpeciesIdx(idx);
  lineBusAck(gLineOut, "species", true);
  return true;
}

static bool cmdUnpair(JsonDocument&, void*) {
  bleClearBonds();
  lineBusAck(gLineOut, "unpair", true);
  return true;
}

static bool cmdOwner(JsonDocument& doc, void*) {
  const char* n = doc["name"];
  if (n) ownerSet(n);
  lineBusAck(gLineOut, "owner", n != nullptr);
  return true;
}

static bool cmdStatus(JsonDocument&, void*) {
  // Dump everything the info screens show. Manual printf rather than
  // ArduinoJson serialize — less heap churn, and the shape is fixed.
  HwBattery hb = hwBattery();
  int vBat = hb.mV;
  int iBat = hb.mA;
  int vBus = hb.usbPresent ? 5000 : 0;
  int pct  = hb.pct;
  char b[560];
  int len = snprintf(b, sizeof(b),
    "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
    "\"name\":\"%s\",\"owner\":\"%s\",\"sec\":%s,"
    "\"bat\":{\"pct\":%d,\"mV\":%d,\"mA\":%d,\"usb\":%s},"
    "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
    "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u},"
    "\"wifi\":{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"},"
    "\"hub\":{\"url\":\"%s\",\"ok\":%s},"
    "\"jiggler\":{\"on\":%s,\"hid\":%s},"
    "\"ota\":{\"slot\":\"%s\"}"
    "}}\n",
    petName(), ownerName(), bleSecure() ? "true" : "false",
    pct, vBat, iBat, (vBus > 4000) ? "true" : "false",
    millis() / 1000, ESP.getFreeHeap(),
    (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()),
    (unsigned long)LittleFS.totalBytes(),
    stats().approvals, stats().denials, statsMedianVelocity(),
    (unsigned long)stats().napSeconds, stats().level,
    (const char*[]){"off","portal","joining","online","failed"}[wifiLinkState()],
    wifiLinkSsid(), wifiLinkIp(),
    httpTransportUrl(), httpTransportHealthy() ? "true" : "false",
    settings().jiggler ? "true" : "false", hidMouseConnected() ? "true" : "false",
    esp_ota_get_running_partition() ? esp_ota_get_running_partition()->label : "?"
  );
  gLineOut.write((const uint8_t*)b, len);
  return true;
}

// {"cmd":"wifi","ssid":"…","pass":"…"} → join now (persists on success)
// {"cmd":"wifi","portal":true}         → (re)enter pairing mode
// {"cmd":"wifi","forget":true}         → wipe creds + radio off
static bool cmdWifi(JsonDocument& doc, void*) {
  if (doc["forget"] | false) {
    wifiLinkForget();
    lineBusAck(gLineOut, "wifi", true);
    return true;
  }
  if (doc["portal"] | false) {
    wifiLinkStartPortal();
    wifiSetupOpen = true;          // main.cpp shows the QR screen
    lineBusAck(gLineOut, "wifi", true);
    return true;
  }
  const char* ssid = doc["ssid"];
  if (!ssid || !ssid[0]) { lineBusAck(gLineOut, "wifi", false); return true; }
  wifiLinkJoin(ssid, doc["pass"] | "");
  lineBusAck(gLineOut, "wifi", true);
  return true;
}

// {"cmd":"hub","url":"http://host:port"} → poll that hub over Wi-Fi (persisted)
// {"cmd":"hub","off":true}               → stop + forget the hub
static void _hubSave(const char* url, const char* token) {
  Preferences p;
  p.begin("buddy", false);
  if (url && url[0]) p.putString("huburl", url); else p.remove("huburl");
  if (token) { if (token[0]) p.putString("hubtok", token); else p.remove("hubtok"); }
  p.end();
}

static bool cmdHub(JsonDocument& doc, void*) {
  if (doc["off"] | false) {
    httpTransportStop();
    _hubSave(nullptr, "");
    lineBusAck(gLineOut, "hub", true);
    return true;
  }
  const char* url = doc["url"];
  if (!url || strncmp(url, "http", 4) != 0) { lineBusAck(gLineOut, "hub", false); return true; }
  const char* token = doc["token"];   // optional fleet auth token
  if (token) httpTransportSetToken(token);
  _hubSave(url, token);
  httpTransportStart(url);
  lineBusAck(gLineOut, "hub", true);
  return true;
}

// {"cmd":"jiggler","on":true|false} → toggle the BLE mouse jiggler mode
static bool cmdJiggler(JsonDocument& doc, void*) {
  if (!doc["on"].is<bool>()) { lineBusAck(gLineOut, "jiggler", false); return true; }
  settings().jiggler = doc["on"].as<bool>();
  jigglerApply(settings().jiggler);
  settingsSave();
  lineBusAck(gLineOut, "jiggler", true);
  return true;
}

// {"cmd":"ota","url":"http://host/firmware.bin"} → pull + flash + reboot.
// Acks first (the pull blocks and then reboots, so this is the last line
// the host hears on the old firmware).
static bool cmdOta(JsonDocument& doc, void*) {
  const char* url = doc["url"];
  if (!url || strncmp(url, "http", 4) != 0) { lineBusAck(gLineOut, "ota", false); return true; }
  lineBusAck(gLineOut, "ota", true);
  otaPull(url);   // returns only on failure; the OTA screen shows the error
  return true;
}

// {"cmd":"vdp","on":true|false} → stream the canvas as base64 stripes (USB)
// {"cmd":"vdp","full":true}     → force a full keyframe
static bool cmdVdp(JsonDocument& doc, void*) {
  lineBusAck(gLineOut, "vdp", virtualDisplayCmd(doc));
  return true;
}

static const CmdEntry APP_CMDS[] = {
  { "name",    cmdName    },
  { "species", cmdSpecies },
  { "unpair",  cmdUnpair  },
  { "owner",   cmdOwner   },
  { "status",  cmdStatus  },
  { "wifi",    cmdWifi    },
  { "hub",     cmdHub     },
  { "jiggler", cmdJiggler },
  { "ota",     cmdOta     },
  { "vdp",     cmdVdp     },
};

bool appCommand(JsonDocument& doc) {
  const char* cmd = doc["cmd"];
  if (!cmd) return false;
  if (lineBusDispatch(APP_CMDS, sizeof(APP_CMDS)/sizeof(APP_CMDS[0]), doc, nullptr))
    return true;
  if (filePushHandle(doc)) return true;
  if (!filePushActive()) return strcmp(cmd, "permission") != 0;
  return false;
}

void appCommandsInit() {
  gLineOut.add(&usbSerialSource());
  gLineOut.add(&bleByteSource());
  gLineOut.add(&httpByteSource());
  filePushInit(&APP_FILE_SINK, &gLineOut);
  vdpOut.add(&usbSerialSource());
  virtualDisplayInit((const uint16_t*)hwCanvas()->getFramebuffer(), W, H, &vdpOut);
#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "dev"
#endif
  httpTransportSetIdentity(bleDeviceName(), BOARD_MODEL_LINE1, BUDDY_FW_VERSION);
  Preferences p;
  p.begin("buddy", true);
  String hub = p.getString("huburl", "");
  String tok = p.getString("hubtok", "");
  p.end();
  if (tok.length()) httpTransportSetToken(tok.c_str());
  if (hub.length()) httpTransportStart(hub.c_str());
}
