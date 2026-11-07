// Glance command handlers + transport wiring. Acks fan out through gLineOut
// (USB + hub + notify socket) per rule 6; vdp stripes stay USB-only — ~1 KB lines would
// flood the hub's 1 Hz POST.
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include "line_bus.h"
#include "transport_usb.h"
#include "transport_http.h"
#include "transport_ws.h"
#include "notify_queue.h"
#include "wifi_link.h"
#include "ota.h"
#include "virtual_display.h"
#include "hw/hw.h"
#include "glance_state.h"

LineOut gLineOut;
static LineOut vdpOut;

static bool cmdStatus(JsonDocument&, void*) {
  // Slim ack — manual printf rather than ArduinoJson serialize keeps the
  // shape fixed and the heap churn zero.
  char b[512];
  int len = snprintf(b, sizeof(b),
    "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
    "\"name\":\"glance\","
    "\"sys\":{\"up\":%lu,\"heap\":%u},"
    "\"wifi\":{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"},"
    "\"hub\":{\"url\":\"%s\",\"ok\":%s},"
    "\"notify\":{\"url\":\"%s\",\"ok\":%s},"
    "\"ota\":{\"slot\":\"%s\"}"
    "}}\n",
    millis() / 1000, ESP.getFreeHeap(),
    (const char*[]){"off","portal","joining","online","failed"}[wifiLinkState()],
    wifiLinkSsid(), wifiLinkIp(),
    httpTransportUrl(), httpTransportHealthy() ? "true" : "false",
    wsTransportUrl(), wsTransportConnected() ? "true" : "false",
    esp_ota_get_running_partition() ? esp_ota_get_running_partition()->label : "?"
  );
  // snprintf returns the would-be length, not the truncated one — a 32-char
  // SSID plus a long hub URL can outgrow the buffer, and an unclamped len
  // would send stack bytes past b and break NDJSON framing.
  if (len >= (int)sizeof(b)) len = sizeof(b) - 1;
  gLineOut.write((const uint8_t*)b, len);
  return true;
}

// {"cmd":"wifi","ssid":"…","pass":"…"} → join now (persists on success)
// {"cmd":"wifi","portal":true}         → (re)enter pairing mode. Headless:
// glance has no QR screen — join the SoftAP and use the portal page.
static bool cmdWifi(JsonDocument& doc, void*) {
  if (doc["portal"] | false) {
    wifiLinkStartPortal();
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
// NVS namespace/keys match buddy's, so a board moving between the two apps
// keeps its hub registration (same reuse story as the Wi-Fi creds).
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

// Server → device notify frames ({"cmd":"notify","v":1,"id",…} or a
// retract). Queued (or already queued) → ack delivered; everything else is
// silent: a full inbox leaves the ask queued server-side for the next connect.
static bool cmdNotify(JsonDocument& doc, void*) {
  NotifyOffer r = notifyOffer(gNotify, doc, millis());
  if (r != NOTIFY_QUEUED && r != NOTIFY_DUPLICATE) return true;
  char line[96];
  size_t n = notifyAckLine(line, sizeof(line), doc["id"].as<const char*>(), "delivered");
  if (n) gLineOut.write((const uint8_t*)line, n);
  return true;
}

// {"cmd":"notifylink","url":"ws(s)://…/api/devices/connect","token":"dtk_…"}
//   → hold the notify socket (persisted; token may be omitted to keep the stored one)
// {"cmd":"notifylink","off":true} → close it and forget url + token
// Not "notify": that name belongs to server frames, and its ack would look
// like an id-less notify ack on the socket. The token lives in its own NVS
// namespace and is never echoed.
static void _notifySave(const char* url, const char* token) {
  Preferences p;
  p.begin("buddy", false);
  if (url) p.putString("notifyurl", url); else p.remove("notifyurl");
  p.end();
  p.begin("secrets", false);
  if (token) p.putString("notifytok", token); else p.remove("notifytok");
  p.end();
}

static String _notifyToken() {
  Preferences p;
  p.begin("secrets", true);
  String t = p.getString("notifytok", "");
  p.end();
  return t;
}

static bool _wsUrlOk(const char* url) {
  return url && (!strncmp(url, "ws://", 5) || !strncmp(url, "wss://", 6))
      && strlen(url) < 128 && !strpbrk(url, "\"\\ ");
}

static bool cmdNotifyLink(JsonDocument& doc, void*) {
  if (doc["off"] | false) {
    wsTransportStop();
    _notifySave(nullptr, nullptr);
    lineBusAck(gLineOut, "notifylink", true);
    return true;
  }
  const char* url = doc["url"];
  const char* tok = doc["token"];
  String stored;
  if (!tok) { stored = _notifyToken(); tok = stored.c_str(); }
  if (!_wsUrlOk(url) || !tok[0] || strlen(tok) >= 96) {
    lineBusAck(gLineOut, "notifylink", false);
    return true;
  }
  _notifySave(url, tok);
  wsTransportStart(url, tok);
  lineBusAck(gLineOut, "notifylink", true);
  return true;
}

// {"cmd":"ota","url":"http://host/firmware.bin"} → pull + flash + reboot.
// Acks first (the pull blocks and then reboots, so this is the last line
// the host hears on the old firmware).
static bool cmdOta(JsonDocument& doc, void*) {
  const char* url = doc["url"];
  if (!url || strncmp(url, "http", 4) != 0) { lineBusAck(gLineOut, "ota", false); return true; }
  lineBusAck(gLineOut, "ota", true);
  otaPull(url);   // returns only on failure
  return true;
}

// {"cmd":"vdp","on":true|false} → stream the canvas as base64 stripes (USB)
// {"cmd":"vdp","full":true}     → force a full keyframe
static bool cmdVdp(JsonDocument& doc, void*) {
  lineBusAck(gLineOut, "vdp", virtualDisplayCmd(doc));
  return true;
}

static const CmdEntry APP_CMDS[] = {
  { "status", cmdStatus },
  { "wifi",   cmdWifi   },
  { "hub",    cmdHub    },
  { "ota",    cmdOta    },
  { "vdp",    cmdVdp    },
  { "notify", cmdNotify },
  { "notifylink", cmdNotifyLink },
};

bool glanceCommand(JsonDocument& doc) {
  const char* cmd = doc["cmd"];
  if (!cmd) return false;
  lineBusDispatch(APP_CMDS, sizeof(APP_CMDS)/sizeof(APP_CMDS[0]), doc, nullptr);
  // Unknown cmds are swallowed: buddy-protocol commands glance doesn't speak
  // (name/owner/species/file push/…) must not fall through to agentApplyJson,
  // which treats any non-time-sync doc as a live snapshot and would fake
  // agent liveness. No "permission" passthrough — glance has no prompt UI.
  return true;
}

void glanceCommandsInit() {
  gLineOut.add(&usbSerialSource());
  gLineOut.add(&httpByteSource());
  gLineOut.add(&wsByteSource());
  vdpOut.add(&usbSerialSource());
  virtualDisplayInit((const uint16_t*)hwCanvas()->getFramebuffer(), W, H, &vdpOut);
#ifndef BUDDY_FW_VERSION
#define BUDDY_FW_VERSION "dev"
#endif
  httpTransportSetIdentity("glance", BOARD_MODEL_LINE1, BUDDY_FW_VERSION);
  // Autostart against the persisted hub so a reboot resumes polling.
  Preferences p;
  p.begin("buddy", true);
  String hub = p.getString("huburl", "");
  String tok = p.getString("hubtok", "");
  p.end();
  if (tok.length()) httpTransportSetToken(tok.c_str());
  if (hub.length()) httpTransportStart(hub.c_str());

  p.begin("buddy", true);
  String nurl = p.getString("notifyurl", "");
  p.end();
  String ntok = _notifyToken();
  if (nurl.length() && ntok.length()) wsTransportStart(nurl.c_str(), ntok.c_str());
}
