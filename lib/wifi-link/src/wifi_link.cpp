#include "wifi_link.h"
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_mac.h>

static WifiLinkState _state = WIFI_LINK_OFF;
static char _ssid[33] = "";
static char _pass[65] = "";
static char _ip[16]   = "";
static char _err[48]  = "";
static char _apSsid[16] = "";
static char _apPass[12] = "";

static bool      _portalUp = false;
static uint32_t  _joinStartMs = 0;
static uint32_t  _portalCloseAtMs = 0;   // grace period so the phone sees "online"
static bool      _pendingSave = false;   // persist creds once the join succeeds

static DNSServer* _dns = nullptr;
static WebServer* _http = nullptr;

static const uint32_t JOIN_TIMEOUT_MS = 20000;
// Keep the AP up briefly after success so the portal status page can show it.
static const uint32_t PORTAL_LINGER_MS = 8000;

// ---------------------------------------------------------------------------
// Portal HTML — one page, inline JS polls /status and refreshes /scan.
// ---------------------------------------------------------------------------
static const char PORTAL_HTML[] PROGMEM = R"html(<!doctype html>
<html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>Buddy Wi-Fi setup</title><style>
body{font-family:-apple-system,system-ui,sans-serif;background:#111;color:#eee;
display:flex;justify-content:center;padding:24px 16px}
main{width:100%;max-width:360px}h1{font-size:20px;margin:0 0 4px}
p.sub{color:#999;margin:0 0 20px;font-size:14px}
label{display:block;font-size:13px;color:#aaa;margin:14px 0 4px}
select,input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
border:1px solid #333;background:#1c1c1e;color:#eee;font-size:16px}
button{width:100%;margin-top:20px;padding:12px;border-radius:8px;border:0;
background:#d97757;color:#fff;font-size:16px;font-weight:600}
button:disabled{opacity:.5}
#st{margin-top:16px;font-size:14px;min-height:20px;color:#999}
#st.ok{color:#5fbf77}#st.bad{color:#e5484d}
a{color:#888;font-size:13px}</style></head><body><main>
<h1>Connect Buddy to Wi-Fi</h1>
<p class=sub>Pick your network and enter its password.</p>
<label>Network</label>
<select id=ssid><option>scanning…</option></select>
<label>Password</label>
<input id=pass type=password autocomplete=off placeholder="Wi-Fi password">
<button id=go onclick="save()">Connect</button>
<div id=st></div>
<p style="margin-top:24px"><a href=# onclick="scan();return false">rescan networks</a></p>
<script>
let joined=false;
function scan(){fetch('/scan').then(r=>r.json()).then(l=>{
  const s=document.getElementById('ssid');s.innerHTML='';
  if(!l.length){s.innerHTML='<option>no networks yet — rescan</option>';return}
  l.forEach(n=>{const o=document.createElement('option');o.value=o.textContent=n;s.appendChild(o)});
})}
function save(){
  const b=document.getElementById('go');b.disabled=true;joined=true;
  const body='ssid='+encodeURIComponent(ssid.value)+'&pass='+encodeURIComponent(pass.value);
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(()=>poll());
}
function poll(){fetch('/status').then(r=>r.json()).then(s=>{
  const el=document.getElementById('st');
  if(s.state=='online'){el.textContent='Connected! Buddy is at '+s.ip;el.className='ok';return}
  if(s.state=='failed'){el.textContent='Failed: '+s.error;el.className='bad';
    document.getElementById('go').disabled=false;return}
  el.textContent='Connecting…';el.className='';setTimeout(poll,1200);
}).catch(()=>setTimeout(poll,1500))}
scan();setTimeout(scan,4000);
</script></main></body></html>)html";

// ---------------------------------------------------------------------------

static void _setErr(const char* e) {
  strncpy(_err, e, sizeof(_err)-1); _err[sizeof(_err)-1] = 0;
}

static void _beginJoin() {
  _err[0] = 0; _ip[0] = 0;
  WiFi.begin(_ssid, _pass);
  _joinStartMs = millis();
  _state = WIFI_LINK_JOINING;
  Serial.printf("[wifi] joining '%s'\n", _ssid);
}

static void _handleRoot() { _http->send_P(200, "text/html", PORTAL_HTML); }

static void _handleScan() {
  // Async scan: kick one off if none done; return what we have so far.
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n = 0; }
  String json = "[";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    if (json.length() > 1) json += ",";
    json += "\""; json += s; json += "\"";
  }
  json += "]";
  _http->send(200, "application/json", json);
}

static void _handleSave() {
  String ssid = _http->arg("ssid");
  String pass = _http->arg("pass");
  if (!ssid.length()) { _http->send(400, "text/plain", "ssid required"); return; }
  strncpy(_ssid, ssid.c_str(), sizeof(_ssid)-1); _ssid[sizeof(_ssid)-1] = 0;
  strncpy(_pass, pass.c_str(), sizeof(_pass)-1); _pass[sizeof(_pass)-1] = 0;
  _pendingSave = true;
  _http->send(200, "text/plain", "ok");
  _beginJoin();
}

static void _handleStatus() {
  char b[128];
  const char* st = _state == WIFI_LINK_ONLINE ? "online"
                 : _state == WIFI_LINK_FAILED ? "failed"
                 : _state == WIFI_LINK_JOINING ? "joining" : "portal";
  snprintf(b, sizeof(b), "{\"state\":\"%s\",\"ip\":\"%s\",\"error\":\"%s\"}", st, _ip, _err);
  _http->send(200, "application/json", b);
}

// Captive-portal detection endpoints (iOS/Android/Windows) → the portal page.
static void _handleCaptive() {
  _http->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  _http->send(302, "text/plain", "");
}

void wifiLinkInit() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(_apSsid, sizeof(_apSsid), "Buddy-%02X%02X", mac[4], mac[5]);
  snprintf(_apPass, sizeof(_apPass), "buddy%02x%02x", mac[4], mac[5]);
  wifiCredsLoad(_ssid, sizeof(_ssid), _pass, sizeof(_pass));
}

bool wifiLinkHasCreds() { return _ssid[0] != 0; }

bool wifiLinkConnect() {
  if (!_ssid[0]) return false;
  WiFi.mode(_portalUp ? WIFI_AP_STA : WIFI_STA);
  _beginJoin();
  return true;
}

void wifiLinkJoin(const char* ssid, const char* pass) {
  strncpy(_ssid, ssid, sizeof(_ssid)-1); _ssid[sizeof(_ssid)-1] = 0;
  strncpy(_pass, pass ? pass : "", sizeof(_pass)-1); _pass[sizeof(_pass)-1] = 0;
  _pendingSave = true;
  WiFi.mode(_portalUp ? WIFI_AP_STA : WIFI_STA);
  _beginJoin();
}

void wifiLinkDisconnect() {
  if (_portalUp) wifiLinkStopPortal();
  WiFi.disconnect(true /*wifioff*/);
  _state = WIFI_LINK_OFF;
  _ip[0] = 0;
}

void wifiLinkForget() {
  wifiCredsClear();
  _ssid[0] = 0; _pass[0] = 0;
  wifiLinkDisconnect();
}

void wifiLinkStartPortal() {
  if (_portalUp) return;
  WiFi.mode(WIFI_AP_STA);                  // STA stays usable for scan + join
  WiFi.softAP(_apSsid, _apPass);
  if (!_dns)  _dns  = new DNSServer();
  if (!_http) _http = new WebServer(80);
  _dns->start(53, "*", WiFi.softAPIP());
  _http->on("/", _handleRoot);
  _http->on("/scan", _handleScan);
  _http->on("/save", HTTP_POST, _handleSave);
  _http->on("/status", _handleStatus);
  _http->on("/generate_204", _handleCaptive);       // Android
  _http->on("/hotspot-detect.html", _handleCaptive); // iOS/macOS
  _http->on("/connecttest.txt", _handleCaptive);     // Windows
  _http->onNotFound(_handleCaptive);
  _http->begin();
  WiFi.scanNetworks(true);                 // warm the SSID list for the page
  _portalUp = true;
  _portalCloseAtMs = 0;
  _err[0] = 0;
  if (_state == WIFI_LINK_OFF || _state == WIFI_LINK_FAILED) _state = WIFI_LINK_PORTAL;
  Serial.printf("[wifi] portal up: AP '%s' pass '%s' ip %s\n",
                _apSsid, _apPass, WiFi.softAPIP().toString().c_str());
}

void wifiLinkStopPortal() {
  if (!_portalUp) return;
  _http->stop();
  _dns->stop();
  WiFi.softAPdisconnect(true);
  _portalUp = false;
  _portalCloseAtMs = 0;
  WiFi.mode(_state == WIFI_LINK_JOINING || _state == WIFI_LINK_ONLINE ? WIFI_STA : WIFI_OFF);
  if (_state == WIFI_LINK_PORTAL) _state = WIFI_LINK_OFF;
  Serial.println("[wifi] portal down");
}

void wifiLinkTick() {
  if (_portalUp) {
    _dns->processNextRequest();
    _http->handleClient();
    // Linger after success so the phone's status poll sees "online".
    if (_portalCloseAtMs && (int32_t)(millis() - _portalCloseAtMs) >= 0)
      wifiLinkStopPortal();
  }

  if (_state == WIFI_LINK_JOINING) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      strncpy(_ip, WiFi.localIP().toString().c_str(), sizeof(_ip)-1);
      _ip[sizeof(_ip)-1] = 0;
      _state = WIFI_LINK_ONLINE;
      if (_pendingSave) { wifiCredsSave(_ssid, _pass); _pendingSave = false; }
      if (_portalUp) _portalCloseAtMs = millis() + PORTAL_LINGER_MS;
      Serial.printf("[wifi] online: %s\n", _ip);
    } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ||
               (millis() - _joinStartMs) > JOIN_TIMEOUT_MS) {
      _setErr(st == WL_NO_SSID_AVAIL ? "network not found"
            : st == WL_CONNECT_FAILED ? "wrong password?" : "timed out");
      WiFi.disconnect();
      _state = _portalUp ? WIFI_LINK_FAILED : WIFI_LINK_OFF;
      if (!_portalUp) WiFi.mode(WIFI_OFF);
      _pendingSave = false;
      Serial.printf("[wifi] join failed: %s\n", _err);
    }
  } else if (_state == WIFI_LINK_ONLINE && WiFi.status() != WL_CONNECTED && !_portalUp) {
    // Dropped link — try to ride it out with one silent rejoin.
    Serial.println("[wifi] link lost, rejoining");
    _beginJoin();
  }
}

WifiLinkState wifiLinkState() { return _state; }
const char* wifiLinkSsid()   { return _ssid; }
const char* wifiLinkIp()     { return _state == WIFI_LINK_ONLINE ? _ip : ""; }
const char* wifiLinkError()  { return _err; }
const char* wifiLinkApSsid() { return _apSsid; }
const char* wifiLinkApPass() { return _apPass; }

void wifiLinkQrText(char* buf, size_t cap) {
  snprintf(buf, cap, "WIFI:T:WPA;S:%s;P:%s;;", _apSsid, _apPass);
}
