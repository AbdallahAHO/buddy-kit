// App-side impl of the wifi-link persistence contract (same pattern as
// faces_store). NVS namespace "buddy", keys wssid/wpass.
#include "wifi_link.h"
#include <Preferences.h>

static Preferences _wifiPrefs;

bool wifiCredsLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap) {
  _wifiPrefs.begin("buddy", true);
  String s = _wifiPrefs.getString("wssid", "");
  String p = _wifiPrefs.getString("wpass", "");
  _wifiPrefs.end();
  strncpy(ssid, s.c_str(), ssidCap-1); ssid[ssidCap-1] = 0;
  strncpy(pass, p.c_str(), passCap-1); pass[passCap-1] = 0;
  return ssid[0] != 0;
}

void wifiCredsSave(const char* ssid, const char* pass) {
  _wifiPrefs.begin("buddy", false);
  _wifiPrefs.putString("wssid", ssid);
  _wifiPrefs.putString("wpass", pass);
  _wifiPrefs.end();
}

void wifiCredsClear() {
  _wifiPrefs.begin("buddy", false);
  _wifiPrefs.remove("wssid");
  _wifiPrefs.remove("wpass");
  _wifiPrefs.end();
}
