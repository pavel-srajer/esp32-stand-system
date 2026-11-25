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
  gCfg.screenOrder = prefs.getString("sorder", d.screenOrder);
  gCfg.note = prefs.getString("note", "");
  gCfg.noteFg = prefs.getUInt("nfg", d.noteFg);
  gCfg.noteBg = prefs.getUInt("nbg", d.noteBg);
  gCfg.noteSize = prefs.getInt("nsize", d.noteSize);
  gCfg.noteStyle = prefs.getInt("nstyle", d.noteStyle);
  gCfg.noteBlink = prefs.getInt("nblink", d.noteBlink);
  gCfg.rainRadiusIdx = prefs.getInt("rrad", d.rainRadiusIdx);
  gCfg.rainMap = prefs.getInt("rmap", d.rainMap);
  gCfg.planeMap = prefs.getInt("pmap", d.planeMap);
  gCfg.cycleSec = prefs.getInt("cycle", d.cycleSec);
  gCfg.ntp1 = prefs.getString("ntp1", d.ntp1);
  gCfg.ntp2 = prefs.getString("ntp2", d.ntp2);
  gCfg.lang = prefs.getInt("lang", d.lang);
  gCfg.detailMask = prefs.getInt("detail2", -1);
  if (gCfg.detailMask < 0)  // starší nastavení bez řádku "letecká společnost" -> zapnout ho
    gCfg.detailMask = prefs.isKey("detail") ? (prefs.getInt("detail", 0xFF) | DET_AIRLINE) : d.detailMask;
  gCfg.speedUnit = prefs.getInt("spdu", d.speedUnit);
  gCfg.detailCycle = prefs.getInt("dcycle", d.detailCycle);
  gCfg.routeFmt = prefs.getInt("rfmt", d.routeFmt);
  gCfg.priceFee = prefs.getFloat("pfee", d.priceFee);
  gCfg.priceVat = prefs.getFloat("pvat", d.priceVat);
  gCfg.priceSrc = prefs.getInt("psrc", d.priceSrc);
  gCfg.adsbUrl = prefs.getString("adsburl", "");
  gCfg.planeSrc = prefs.getInt("plsrc", d.planeSrc);
  for (int i = 0; i < PRICE_WINS; i++) {
    char k[8];
    PriceWin& w = gCfg.priceWin[i];
    snprintf(k, sizeof k, "pw%don", i);
    w.on = prefs.getBool(k, false);
    snprintf(k, sizeof k, "pw%dnm", i);
    w.name = prefs.getString(k, "");
    snprintf(k, sizeof k, "pw%ddu", i);
    w.dur = prefs.getInt(k, 8);
    snprintf(k, sizeof k, "pw%dfr", i);
    w.from = prefs.getInt(k, 0);
    snprintf(k, sizeof k, "pw%dto", i);
    w.to = prefs.getInt(k, 24 * 60);
  }
  gCfg.rbMode = prefs.getInt("rbmode", d.rbMode);
  gCfg.rbHour = prefs.getInt("rbhour", d.rbHour);
  gCfg.rbMinKB = prefs.getInt("rbmin", d.rbMinKB);
  gCfg.webUser = prefs.getString("wuser", d.webUser);
  gCfg.webPass = prefs.getString("wpass", "");
  gCfg.alertMask = prefs.getInt("almask", d.alertMask);
  gCfg.watchList = prefs.getString("watch", "");
  gCfg.alertSec = prefs.getInt("alsec", d.alertSec);
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
  prefs.putString("sorder", gCfg.screenOrder);
  prefs.putInt("rrad", gCfg.rainRadiusIdx);
  prefs.putInt("rmap", gCfg.rainMap);
  prefs.putInt("pmap", gCfg.planeMap);
  prefs.putInt("cycle", gCfg.cycleSec);
  prefs.putString("ntp1", gCfg.ntp1);
  prefs.putString("ntp2", gCfg.ntp2);
  prefs.putInt("lang", gCfg.lang);
  prefs.putInt("detail2", gCfg.detailMask);
  prefs.putInt("spdu", gCfg.speedUnit);
  prefs.putInt("dcycle", gCfg.detailCycle);
  prefs.putInt("rfmt", gCfg.routeFmt);
  prefs.putFloat("pfee", gCfg.priceFee);
  prefs.putFloat("pvat", gCfg.priceVat);
  prefs.putInt("psrc", gCfg.priceSrc);
  prefs.putString("adsburl", gCfg.adsbUrl);
  prefs.putInt("plsrc", gCfg.planeSrc);
  for (int i = 0; i < PRICE_WINS; i++) {
    char k[8];
    const PriceWin& w = gCfg.priceWin[i];
    snprintf(k, sizeof k, "pw%don", i);
    prefs.putBool(k, w.on);
    snprintf(k, sizeof k, "pw%dnm", i);
    prefs.putString(k, w.name);
    snprintf(k, sizeof k, "pw%ddu", i);
    prefs.putInt(k, w.dur);
    snprintf(k, sizeof k, "pw%dfr", i);
    prefs.putInt(k, w.from);
    snprintf(k, sizeof k, "pw%dto", i);
    prefs.putInt(k, w.to);
  }
  prefs.putInt("rbmode", gCfg.rbMode);
  prefs.putInt("rbhour", gCfg.rbHour);
  prefs.putInt("rbmin", gCfg.rbMinKB);
  prefs.putString("wuser", gCfg.webUser);
  prefs.putString("wpass", gCfg.webPass);
  prefs.putInt("almask", gCfg.alertMask);
  prefs.putString("watch", gCfg.watchList);
  prefs.putInt("alsec", gCfg.alertSec);
  prefs.end();
}

void settingsSaveNote() {
  prefs.begin("cfg", false);
  prefs.putString("note", gCfg.note);
  prefs.putUInt("nfg", gCfg.noteFg);
  prefs.putUInt("nbg", gCfg.noteBg);
  prefs.putInt("nsize", gCfg.noteSize);
  prefs.putInt("nstyle", gCfg.noteStyle);
  prefs.putInt("nblink", gCfg.noteBlink);
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

void settingsSetOtaUrl(const String& url) {
  prefs.begin("cfg", false);
  prefs.putString("otaurl", url);
  prefs.end();
}

String settingsTakeOtaUrl() {
  prefs.begin("cfg", false);
  String u = prefs.getString("otaurl", "");
  if (u.length()) prefs.remove("otaurl");
  prefs.end();
  return u;
}

// ---------------------------------------------------------------------------
// Záloha a obnova. Klíče jsou čitelné názvy; obnova bere jen ty, které v souboru jsou,
// takže jde nahrát i starší nebo ručně zkrácenou zálohu.
#define FIELDS(X)                                                                                         \
  X(lat, lat) X(lon, lon) X(wsi, wsi) X(name, name) X(elev, elev) X(range, rangeIdx) X(autoUpdate, autoUpdate) \
  X(blPin, blPin) X(tDay, tDay) X(tEve, tEve) X(tNight, tNight) X(bDay, bDay) X(bEve, bEve) X(bNight, bNight)  \
  X(screens, screens) X(screenOrder, screenOrder) X(note, note) X(noteFg, noteFg) X(noteBg, noteBg)            \
  X(noteSize, noteSize) X(noteStyle, noteStyle) X(noteBlink, noteBlink) X(rainRadius, rainRadiusIdx)           \
  X(rainMap, rainMap) X(planeMap, planeMap) X(cycleSec, cycleSec) X(ntp1, ntp1) X(ntp2, ntp2) X(lang, lang)    \
  X(detailMask, detailMask) X(detailCycle, detailCycle) X(speedUnit, speedUnit) X(routeFmt, routeFmt)          \
  X(priceFee, priceFee) X(priceVat, priceVat) X(adsbUrl, adsbUrl) X(planeSrc, planeSrc) X(priceSrc, priceSrc)  \
  X(rbMode, rbMode) X(rbHour, rbHour) X(rbMinKB, rbMinKB) X(webUser, webUser) X(alertMask, alertMask)          \
  X(watchList, watchList) X(alertSec, alertSec)

template <typename T>
static void getField(JsonObjectConst o, const char* k, T& v) {
  if (o[k].is<T>()) v = o[k].as<T>();
}
static void getField(JsonObjectConst o, const char* k, String& v) {
  if (o[k].is<const char*>()) v = o[k].as<const char*>();
}
static void getField(JsonObjectConst o, const char* k, float& v) {
  if (o[k].is<float>()) v = o[k].as<float>();
}

void settingsToJson(JsonObject o, bool secrets) {
  o["app"] = "esp32-stand-system";
#define PUT(key, f) o[#key] = gCfg.f;
  FIELDS(PUT)
#undef PUT
  JsonArray pw = o["priceWin"].to<JsonArray>();
  for (const PriceWin& w : gCfg.priceWin) {
    JsonObject x = pw.add<JsonObject>();
    x["on"] = w.on;
    x["name"] = w.name;
    x["dur"] = w.dur;
    x["from"] = w.from;
    x["to"] = w.to;
  }
  if (secrets) {
    o["ssid"] = gCfg.ssid;
    o["pass"] = gCfg.pass;
    o["webPass"] = gCfg.webPass;
  }
}

void settingsFromJson(JsonObjectConst o) {
#define GET(key, f) getField(o, #key, gCfg.f);
  FIELDS(GET)
#undef GET
  JsonArrayConst pw = o["priceWin"];
  for (int i = 0; i < PRICE_WINS && i < (int)pw.size(); i++) {
    PriceWin& w = gCfg.priceWin[i];
    JsonObjectConst x = pw[i];
    getField(x, "on", w.on);
    getField(x, "name", w.name);
    getField(x, "dur", w.dur);
    getField(x, "from", w.from);
    getField(x, "to", w.to);
  }
  getField(o, "ssid", gCfg.ssid);
  getField(o, "pass", gCfg.pass);
  getField(o, "webPass", gCfg.webPass);
  settingsSave();
  settingsSaveNote();
}
