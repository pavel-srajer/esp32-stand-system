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
  gCfg.blPin = prefs.getInt("bl", d.blPin);
  gCfg.tDay = prefs.getInt("tday", d.tDay);
  gCfg.tEve = prefs.getInt("teve", d.tEve);
  gCfg.tNight = prefs.getInt("tnight", d.tNight);
  gCfg.bDay = prefs.getInt("bday", d.bDay);
  gCfg.bEve = prefs.getInt("beve", d.bEve);
  gCfg.bNight = prefs.getInt("bnight", d.bNight);
  gCfg.screens = prefs.getInt("screens", d.screens);
  gCfg.rainRadiusIdx = prefs.getInt("rrad", d.rainRadiusIdx);
  gCfg.rainMap = prefs.getInt("rmap", d.rainMap);
  gCfg.planeMap = prefs.getInt("pmap", d.planeMap);
  gCfg.cycleSec = prefs.getInt("cycle", d.cycleSec);
  gCfg.ntp1 = prefs.getString("ntp1", d.ntp1);
  gCfg.ntp2 = prefs.getString("ntp2", d.ntp2);
  gCfg.lang = prefs.getInt("lang", d.lang);
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
  prefs.putInt("bl", gCfg.blPin);
  prefs.putInt("tday", gCfg.tDay);
  prefs.putInt("teve", gCfg.tEve);
  prefs.putInt("tnight", gCfg.tNight);
  prefs.putInt("bday", gCfg.bDay);
  prefs.putInt("beve", gCfg.bEve);
  prefs.putInt("bnight", gCfg.bNight);
  prefs.putInt("screens", gCfg.screens);
  prefs.putInt("rrad", gCfg.rainRadiusIdx);
  prefs.putInt("rmap", gCfg.rainMap);
  prefs.putInt("pmap", gCfg.planeMap);
  prefs.putInt("cycle", gCfg.cycleSec);
  prefs.putString("ntp1", gCfg.ntp1);
  prefs.putString("ntp2", gCfg.ntp2);
  prefs.putInt("lang", gCfg.lang);
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

void settingsSetOtaFlag(bool on) {
  prefs.begin("cfg", false);
  prefs.putBool("otanow", on);
  prefs.end();
}

bool settingsTakeOtaFlag() {
  prefs.begin("cfg", false);
  bool f = prefs.getBool("otanow", false);
  if (f) prefs.remove("otanow");
  prefs.end();
  return f;
}
