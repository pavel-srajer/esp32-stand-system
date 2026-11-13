#include "settings.h"
#include <Preferences.h>

Settings gCfg;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("cfg", true);
  Settings d;
  gCfg.ssid = prefs.getString("ssid", "");
  gCfg.pass = prefs.getString("pass", "");
  gCfg.lat = prefs.getFloat("lat", d.lat);
  gCfg.lon = prefs.getFloat("lon", d.lon);
  gCfg.wsi = prefs.getString("wsi", d.wsi);
  gCfg.name = prefs.getString("name", d.name);
  gCfg.elev = prefs.getInt("elev", d.elev);
  gCfg.rangeIdx = prefs.getInt("range", d.rangeIdx);
  gCfg.autoUpdate = prefs.getBool("autoupd", d.autoUpdate);
  prefs.end();
}

void settingsSave() {
  prefs.begin("cfg", false);
  prefs.putString("ssid", gCfg.ssid);
  prefs.putString("pass", gCfg.pass);
  prefs.putFloat("lat", gCfg.lat);
  prefs.putFloat("lon", gCfg.lon);
  prefs.putString("wsi", gCfg.wsi);
  prefs.putString("name", gCfg.name);
  prefs.putInt("elev", gCfg.elev);
  prefs.putInt("range", gCfg.rangeIdx);
  prefs.putBool("autoupd", gCfg.autoUpdate);
  prefs.end();
}

void settingsSaveRange() {
  prefs.begin("cfg", false);
  prefs.putInt("range", gCfg.rangeIdx);
  prefs.end();
}

bool settingsHasWifi() { return gCfg.ssid.length() > 0; }

void settingsSetPortalFlag(bool on) {
  prefs.begin("cfg", false);
  prefs.putBool("portal", on);
  prefs.end();
}

bool settingsTakePortalFlag() {
  prefs.begin("cfg", false);
  bool f = prefs.getBool("portal", false);
  if (f) prefs.remove("portal");
  prefs.end();
  return f;
}
