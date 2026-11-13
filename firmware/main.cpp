// ESP32-C3 Super Mini + GC9A01 (1.28" kulatý displej)
// Letadla v okolí (ADS-B: airplanes.live / adsb.lol) + aktuální meteodata ČHMI.
//
// Tlačítko BOOT (GPIO 9):
//   krátký stisk  -> další obrazovka (radar -> detail letadla -> počasí -> hodiny)
//   dlouhý stisk  -> radar: změna dosahu 10/25/50/100 km
//                    detail: další letadlo (podle vzdálenosti)
//                    počasí/hodiny: okamžitá aktualizace dat
//   držet 5 s     -> konfigurační hotspot "Letadla-XXXX" (WiFi, poloha, stanice)
//
// Při prvním spuštění (nebo když se nejde připojit k WiFi) se zařízení samo
// přepne do konfiguračního hotspotu bez hesla, nastavení na http://192.168.4.1

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

#include "display.h"
#include "settings.h"
#include "portal.h"
#include "ota.h"
#include "fonts_cz.h"
#include "http_body.h"
#include "sysinfo.h"
#include "brightness.h"
#include "radar.h"
#include "forecast.h"
#include "logo.h"

// ===================================================================
//  Konstanty
// ===================================================================
static const int PIN_BUTTON = 9;  // tlačítko BOOT

// obrazovky (pořadí = bity v gCfg.screens)
enum Screen { SCR_RADAR, SCR_DETAIL, SCR_FORECAST, SCR_RAIN, SCR_WEATHER, SCR_CLOCK, SCR_COUNT };
static const float RAIN_RADII[] = {25, 50, 100, 200};
static const uint32_t FORECAST_REFRESH_MS = 900000;  // 15 min
static const uint32_t RAIN_REFRESH_MS = 300000;      // ČHMI radar každých 5 min
static const uint32_t PLANES_REFRESH_MS = 10000;    // API povolují max ~1 dotaz/s
static const uint32_t WEATHER_REFRESH_MS = 600000;  // ČHMI publikuje 10min data
static const uint32_t PORTAL_IDLE_RESTART_MS = 300000;  // portál bez aktivity -> zkus znovu WiFi
static const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
// ESP32-C3 Super Mini má špatně navrženou anténu; s plným výkonem se často
// nepřipojí k WiFi. Snížení vysílacího výkonu to typicky vyřeší.
static const bool WIFI_LOW_TX_POWER = true;
static const float RANGES_KM[] = {10, 25, 50, 100};
static const int NUM_RANGES = sizeof(RANGES_KM) / sizeof(RANGES_KM[0]);

// API vracejí stejný formát (readsb "v2"), při výpadku se zkusí druhé.
// Parametry: lat, lon, poloměr v NM.
static const char* PLANE_APIS[] = {
    "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
    "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
};

// Kódy veličin v 10min datech ČHMI (soubor 10m-<WSI>-<YYYYMMDD>.json)
static const char* EL_TEMP = "T";        // teplota vzduchu 2 m [°C]
static const char* EL_HUM = "H";         // relativní vlhkost [%]
static const char* EL_PRESS = "P";       // tlak na stanici [hPa]
static const char* EL_WIND = "F";        // průměrná rychlost větru [m/s]
static const char* EL_GUST = "Fmax";     // maximální náraz [m/s]
static const char* EL_WDIR = "D";        // směr větru [°]
static const char* EL_RAIN = "SRA10M";   // srážky za 10 min [mm]

static const int32_t ALT_UNKNOWN = INT32_MIN;
static const int32_t ALT_GROUND = INT32_MIN + 1;

// ===================================================================
//  Sdílená data (síťový task -> vykreslování)
// ===================================================================
struct Plane {
  char flight[10];
  char type[6];
  char reg[10];
  int32_t alt;     // ft
  float gs;        // kt
  float track;     // °
  int16_t vrate;   // ft/min
  float lat, lon;
  float distKm;
  float brg;       // azimut od domova
};

static const int MAX_PLANES = 40;
struct PlaneData {
  Plane p[MAX_PLANES];
  int count = 0;
  bool ok = false;
  uint32_t updatedMs = 0;
  char err[32] = "";
};

struct Weather {
  bool ok = false;
  float t = NAN, h = NAN, p = NAN, f = NAN, fmax = NAN, d = NAN, rain = NAN;
  time_t obs = 0;
  uint32_t updatedMs = 0;
  char err[32] = "";
};

static PlaneData gPlanes;
static Weather gWeather;
static SemaphoreHandle_t gMutex;
static volatile int gRangeIdx = 1;
static bool gPortalMode = false;
static volatile bool gForcePlanes = true;
static volatile bool gForceWeather = true;
static volatile bool gForceForecast = false;
static volatile bool gForceRain = false;

static bool scrOn(int s) {
  int m = (gCfg.screens & ((1 << SCR_COUNT) - 1)) ? gCfg.screens : 1;
  return m & (1 << s);
}

// ===================================================================
//  Pomocné funkce
// ===================================================================
static void copyTrim(char* dst, size_t n, const char* src) {
  while (*src == ' ') src++;
  strlcpy(dst, src, n);
  int l = strlen(dst);
  while (l > 0 && dst[l - 1] == ' ') dst[--l] = 0;
}

static void distBearing(float lat, float lon, float& distKm, float& brg) {
  const float R = 6371.0f;
  float dLat = (lat - gCfg.lat) * DEG_TO_RAD;
  float dLon = (lon - gCfg.lon) * DEG_TO_RAD * cosf(gCfg.lat * DEG_TO_RAD);
  float n = dLat * R, e = dLon * R;
  distKm = sqrtf(n * n + e * e);
  brg = atan2f(e, n) * RAD_TO_DEG;
  if (brg < 0) brg += 360;
}

static const char* compass8(float deg) {
  static const char* d[] = {"S", "SV", "V", "JV", "J", "JZ", "Z", "SZ"};
  int i = (int)floorf(fmodf(deg + 22.5f + 360.0f, 360.0f) / 45.0f) & 7;
  return d[i];
}

static const char* planesWord(int n) {
  if (n == 1) return "letadlo";
  if (n >= 2 && n <= 4) return "letadla";
  return "letadel";
}

// číslo s desetinnou čárkou
static void fmtDec(char* buf, size_t n, float v, int dec) {
  snprintf(buf, n, "%.*f", dec, v);
  for (char* c = buf; *c; c++)
    if (*c == '.') *c = ',';
}

// "2025-11-13T10:20:00Z" -> unix time (UTC)
static time_t isoToUtc(const char* s) {
  int Y, M, D, h, m, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
  Y -= M <= 2;
  long era = (Y >= 0 ? Y : Y - 399) / 400;
  unsigned yoe = (unsigned)(Y - era * 400);
  unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + h * 3600 + m * 60 + sec;
}

static bool timeValid() { return time(nullptr) > 1700000000; }

// ===================================================================
//  ADS-B
// ===================================================================
static void insertPlane(PlaneData& d, const Plane& pl) {
  int pos = d.count;
  if (d.count == MAX_PLANES) {
    if (pl.distKm >= d.p[MAX_PLANES - 1].distKm) return;
    pos = MAX_PLANES - 1;
  } else {
    d.count++;
  }
  while (pos > 0 && d.p[pos - 1].distKm > pl.distKm) {
    d.p[pos] = d.p[pos - 1];
    pos--;
  }
  d.p[pos] = pl;
}

static bool fetchPlanesFrom(const char* tmpl, PlaneData& out) {
  float rangeKm = RANGES_KM[gRangeIdx];
  int nm = (int)ceilf(rangeKm / 1.852f) + 1;
  char url[128];
  snprintf(url, sizeof url, tmpl, gCfg.lat, gCfg.lon, nm);

  WiFiClientSecure client;
  client.setInsecure();  // bez ověřování certifikátu (veřejná data)
  HTTPClient http;
  BodyStream::prepare(http);  // HTTP/1.1 (HTTP/1.0 některé servery odmítají s 426)
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("esp32-stand-system/1.0");
  if (!http.begin(client, url)) {
    strlcpy(out.err, "HTTP begin", sizeof out.err);
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[ADSB] %s -> HTTP %d\n", url, code);
    snprintf(out.err, sizeof out.err, "%s HTTP %d", strstr(tmpl, "adsb.lol") ? "adsb.lol" : "airplanes.live", code);
    http.end();
    return false;
  }

  BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  for (const char* k : {"hex", "flight", "r", "t", "alt_baro", "gs", "track", "baro_rate", "lat", "lon"})
    f[k] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[ADSB] JSON: %s\n", err.c_str());
    snprintf(out.err, sizeof out.err, "JSON %s", err.c_str());
    return false;
  }

  out.count = 0;
  for (JsonObject a : doc["ac"].as<JsonArray>()) {
    if (!a["lat"].is<float>() || !a["lon"].is<float>()) continue;
    Plane pl{};
    pl.lat = a["lat"];
    pl.lon = a["lon"];
    distBearing(pl.lat, pl.lon, pl.distKm, pl.brg);
    if (pl.distKm > rangeKm) continue;

    copyTrim(pl.flight, sizeof pl.flight, a["flight"] | "");
    copyTrim(pl.reg, sizeof pl.reg, a["r"] | "");
    copyTrim(pl.type, sizeof pl.type, a["t"] | "");
    if (!pl.flight[0]) strlcpy(pl.flight, pl.reg[0] ? pl.reg : (a["hex"] | "?"), sizeof pl.flight);

    JsonVariant alt = a["alt_baro"];
    if (alt.is<const char*>()) pl.alt = ALT_GROUND;  // "ground"
    else if (alt.is<float>()) pl.alt = (int32_t)alt.as<float>();
    else pl.alt = ALT_UNKNOWN;

    pl.gs = a["gs"] | NAN;
    pl.track = a["track"] | NAN;
    pl.vrate = a["baro_rate"] | 0;
    insertPlane(out, pl);
  }
  out.ok = true;
  out.err[0] = 0;
  Serial.printf("[ADSB] %d letadel do %.0f km (%s)\n", out.count, rangeKm, url);
  return true;
}

static void fetchPlanes() {
  static PlaneData tmp;
  static int apiIdx = 0;
  bool ok = false;
  for (int i = 0; i < 2 && !ok; i++) {
    ok = fetchPlanesFrom(PLANE_APIS[apiIdx], tmp);
    if (!ok) apiIdx = (apiIdx + 1) % 2;
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  if (ok) {
    tmp.updatedMs = millis();
    gPlanes = tmp;
  } else {
    strlcpy(gPlanes.err, tmp.err, sizeof gPlanes.err);
  }
  xSemaphoreGive(gMutex);
}

// ===================================================================
//  ČHMI – 10minutová data, streamové parsování (soubor má ~200 kB)
// ===================================================================
struct ElemVal {
  char code[12];
  char dt[24];
  float val;
};

struct ChmiParse {
  ElemVal el[32];
  int nEl = 0;
  int rows = 0;
  float rainSum = 0;
  bool rainAny = false;
  int iEl = 1, iDt = 2, iVal = 3;  // výchozí pořadí: STATION,ELEMENT,DT,VAL,...

  void setHeader(char* h) {
    int idx = 0;
    for (char* tok = strtok(h, ","); tok; tok = strtok(nullptr, ","), idx++) {
      while (*tok == ' ') tok++;
      if (!strcmp(tok, "ELEMENT")) iEl = idx;
      else if (!strcmp(tok, "DT")) iDt = idx;
      else if (!strcmp(tok, "VAL")) iVal = idx;
    }
    Serial.printf("[CHMI] header: ELEMENT=%d DT=%d VAL=%d\n", iEl, iDt, iVal);
  }

  void row(char* r) {
    char* fld[10];
    int nf = 0;
    char* s = r;
    while (nf < 10) {
      fld[nf++] = s;
      char* c = strchr(s, ',');
      if (!c) break;
      *c = 0;
      s = c + 1;
    }
    for (int i = 0; i < nf; i++) {  // ořez mezer a uvozovek
      char* f = fld[i];
      while (*f == ' ' || *f == '"') f++;
      int l = strlen(f);
      while (l > 0 && (f[l - 1] == ' ' || f[l - 1] == '"')) f[--l] = 0;
      fld[i] = f;
    }
    int need = max(iEl, max(iDt, iVal));
    if (nf <= need) return;
    const char* code = fld[iEl];
    const char* dt = fld[iDt];
    const char* vs = fld[iVal];
    if (strlen(dt) < 16 || dt[10] != 'T') return;  // není řádek s daty
    if (!*vs || !strcmp(vs, "null")) return;
    float v = atof(vs);
    rows++;
    if (!strcmp(code, EL_RAIN)) {
      rainSum += v;
      rainAny = true;
    }
    for (int i = 0; i < nEl; i++) {
      if (!strcmp(el[i].code, code)) {
        if (strcmp(dt, el[i].dt) >= 0) {
          strlcpy(el[i].dt, dt, sizeof el[i].dt);
          el[i].val = v;
        }
        return;
      }
    }
    if (nEl < 32) {
      strlcpy(el[nEl].code, code, sizeof el[nEl].code);
      strlcpy(el[nEl].dt, dt, sizeof el[nEl].dt);
      el[nEl].val = v;
      nEl++;
    }
  }

  const ElemVal* get(const char* code) const {
    for (int i = 0; i < nEl; i++)
      if (!strcmp(el[i].code, code)) return &el[i];
    return nullptr;
  }
  float val(const char* code) const {
    const ElemVal* e = get(code);
    return e ? e->val : NAN;
  }
};

static bool fetchChmiDay(time_t day, ChmiParse& cp, int& httpCode) {
  struct tm u;
  gmtime_r(&day, &u);
  char url[160];
  snprintf(url, sizeof url,
           "https://opendata.chmi.cz/meteorology/climate/now/data/10m-%s-%04d%02d%02d.json",
           gCfg.wsi.c_str(), u.tm_year + 1900, u.tm_mon + 1, u.tm_mday);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
  if (!http.begin(client, url)) return false;
  httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[CHMI] %s -> HTTP %d\n", url, httpCode);
    http.end();
    return false;
  }

  BodyStream r(http.getStreamPtr(), BodyStream::isChunked(http));
  static const char HDR[] = "\"header\"";
  size_t hi = 0;
  char rowBuf[160];
  int rl = 0;
  bool cap = false;
  int ch;
  uint32_t t0 = millis();
  while ((ch = r.read()) >= 0) {
    if (!cap) {
      if (ch == HDR[hi]) {
        if (!HDR[++hi]) {  // našli jsme "header" -> načti řetězec s názvy sloupců
          hi = 0;
          while ((ch = r.read()) >= 0 && ch != '"') {}
          char h[128];
          int hl = 0;
          while ((ch = r.read()) >= 0 && ch != '"')
            if (hl < (int)sizeof h - 1) h[hl++] = ch;
          h[hl] = 0;
          cp.setHeader(h);
          continue;
        }
      } else {
        hi = (ch == HDR[0]) ? 1 : 0;
      }
    }
    if (ch == '[') {  // začátek (nejvnitřnějšího) pole = řádek dat
      cap = true;
      rl = 0;
      continue;
    }
    if (cap) {
      if (ch == ']') {
        rowBuf[rl] = 0;
        cp.row(rowBuf);
        cap = false;
      } else if (rl < (int)sizeof rowBuf - 1) {
        rowBuf[rl++] = ch;
      } else {
        cap = false;  // příliš dlouhé, nejde o řádek dat
      }
    }
  }
  http.end();
  Serial.printf("[CHMI] %s: %d hodnot, %d veličin, %lu ms\n", url, cp.rows, cp.nEl,
                (unsigned long)(millis() - t0));
  return cp.rows > 0;
}

static bool fetchWeather() {
  static ChmiParse cp;
  Weather w;
  time_t now = time(nullptr);
  int code = 0;
  cp = ChmiParse();
  bool ok = fetchChmiDay(now, cp, code);
  if (!ok || !cp.get(EL_TEMP)) {  // krátce po půlnoci UTC ještě nemusí být dnešní soubor
    cp = ChmiParse();
    ok = fetchChmiDay(now - 86400, cp, code);
  }
  if (ok) {
    for (int i = 0; i < cp.nEl; i++)
      Serial.printf("  %-8s %-21s %8.2f\n", cp.el[i].code, cp.el[i].dt, cp.el[i].val);
    w.t = cp.val(EL_TEMP);
    w.h = cp.val(EL_HUM);
    w.p = cp.val(EL_PRESS);
    w.f = cp.val(EL_WIND);
    w.fmax = cp.val(EL_GUST);
    w.d = cp.val(EL_WDIR);
    w.rain = cp.rainAny ? cp.rainSum : NAN;
    const ElemVal* e = cp.get(EL_TEMP);
    if (!e) e = &cp.el[0];
    w.obs = isoToUtc(e->dt);
    w.ok = true;
    w.updatedMs = millis();
  }
  xSemaphoreTake(gMutex, portMAX_DELAY);
  if (ok) gWeather = w;
  else snprintf(gWeather.err, sizeof gWeather.err, "ČHMI HTTP %d", code);
  xSemaphoreGive(gMutex);
  return ok;
}

// ===================================================================
//  Síťový task
// ===================================================================
static void requestPortal() {
  settingsSetPortalFlag(true);
  ESP.restart();
}

static void ensureWiFi() {
  static bool everConnected = false;
  static int failures = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] připojuji k %s...\n", gCfg.ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(gCfg.ssid.c_str(), gCfg.pass.c_str());
  if (WIFI_LOW_TX_POWER) WiFi.setTxPower(WIFI_POWER_8_5dBm);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) vTaskDelay(pdMS_TO_TICKS(250));
  if (WiFi.status() == WL_CONNECTED) {
    everConnected = true;
    failures = 0;
    Serial.printf("[WiFi] OK, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFi] nepodařilo se připojit");
    // od zapnutí se ještě nikdy nepřipojilo (špatné heslo, jiná síť...) -> hotspot
    if (!everConnected && ++failures >= 3) requestPortal();
  }
}

static void netTask(void*) {
  uint32_t lastPlanes = 0, lastWeather = 0;
  uint32_t weatherInterval = WEATHER_REFRESH_MS;
  for (;;) {
    ensureWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t now = millis();
      if (gForcePlanes || now - lastPlanes >= PLANES_REFRESH_MS) {
        gForcePlanes = false;
        fetchPlanes();
        lastPlanes = millis();
      }
      if (scrOn(SCR_WEATHER) && timeValid() && (gForceWeather || millis() - lastWeather >= weatherInterval)) {
        gForceWeather = false;
        bool ok = fetchWeather();
        weatherInterval = ok ? WEATHER_REFRESH_MS : 60000;  // při chybě zkus za minutu
        lastWeather = millis();
      }
      logoService();  // logo pro obrazovku Detail (jen když nějaké čeká)
      // předpověď (Open-Meteo) – pro předpověď, meteoradar i hodiny
      static uint32_t lastFc = 0, fcInterval = FORECAST_REFRESH_MS;
      static bool fcDone = false;
      if ((scrOn(SCR_FORECAST) || scrOn(SCR_RAIN) || scrOn(SCR_CLOCK)) &&
          (gForceForecast || !fcDone || millis() - lastFc >= fcInterval)) {
        gForceForecast = false;
        fcDone = true;
        fcInterval = forecastUpdate() ? FORECAST_REFRESH_MS : 120000;
        lastFc = millis();
      }
      // meteoradar ČHMI
      static uint32_t lastRain = 0;
      static bool rainDone = false;
      if (scrOn(SCR_RAIN) && (gForceRain || !rainDone || millis() - lastRain >= RAIN_REFRESH_MS)) {
        gForceRain = false;
        rainDone = true;
        radarUpdate(RAIN_RADII[constrain(gCfg.rainRadiusIdx, 0, 3)]);
        lastRain = millis();
      }
      // OTA: první kontrola minutu po startu, pak jednou denně; nebo na požádání z webu
      static uint32_t lastOta = 0;
      static bool otaChecked = false;
      if (gOtaRequest) {
        gOtaRequest = false;
        otaCheck(true);
        lastOta = millis();
        otaChecked = true;
      } else if ((!otaChecked && millis() > 60000) || (otaChecked && millis() - lastOta > 86400000UL)) {
        otaChecked = true;
        lastOta = millis();
        // vývojová verze ("dev") se automaticky nepřepisuje
        otaCheck(gCfg.autoUpdate && FW_VERSION[0] == 'v');
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ===================================================================
//  Grafika
// ===================================================================
static LGFX tft;
static LGFX_Sprite spr(&tft);

static const int CX = 120, CY = 120;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lgfx::color565(r, g, b); }

static uint16_t altColor(int32_t alt) {
  if (alt == ALT_GROUND) return rgb(140, 140, 140);
  if (alt == ALT_UNKNOWN) return rgb(255, 255, 255);
  if (alt < 3000) return rgb(0, 255, 80);
  if (alt < 10000) return rgb(255, 230, 0);
  if (alt < 20000) return rgb(255, 140, 0);
  if (alt < 30000) return rgb(255, 60, 160);
  return rgb(0, 200, 255);
}

static uint16_t tempColor(float t) {
  if (isnan(t)) return rgb(200, 200, 200);
  if (t < -10) return rgb(160, 120, 255);
  if (t < 0) return rgb(80, 160, 255);
  if (t < 10) return rgb(0, 220, 255);
  if (t < 20) return rgb(80, 255, 80);
  if (t < 28) return rgb(255, 210, 0);
  return rgb(255, 70, 40);
}

static void drawPlaneIcon(int x, int y, float track, uint16_t col, float s = 7) {
  if (isnan(track)) {
    spr.fillCircle(x, y, 3, col);
    return;
  }
  float a = track * DEG_TO_RAD, sa = sinf(a), ca = cosf(a);
  auto P = [&](float px, float py, int& ox, int& oy) {
    ox = x + (int)lroundf(px * ca - py * sa);
    oy = y + (int)lroundf(px * sa + py * ca);
  };
  int x0, y0, x1, y1, x2, y2, x3, y3;
  P(0, -s, x0, y0);             // nos
  P(-s * 0.7f, s * 0.7f, x1, y1);
  P(0, s * 0.3f, x2, y2);
  P(s * 0.7f, s * 0.7f, x3, y3);
  spr.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  spr.fillTriangle(x0, y0, x2, y2, x3, y3, col);
}

static void altText(char* buf, size_t n, int32_t alt) {
  if (alt == ALT_GROUND) strlcpy(buf, "na zemi", n);
  else if (alt == ALT_UNKNOWN) strlcpy(buf, "výška ?", n);
  else snprintf(buf, n, "%ld m", lroundf(alt * 0.3048f / 10) * 10L);
}

static void drawWifiDot() {
  if (WiFi.status() != WL_CONNECTED) spr.fillCircle(CX, 8, 4, rgb(255, 0, 0));
}

static void drawCenteredMsg(const char* a, const char* b = nullptr) {
  spr.setFont(&fontCZ_16);
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setTextColor(rgb(255, 255, 255));
  spr.drawString(a, CX, b ? CY - 10 : CY);
  if (b) {
    spr.setFont(&fontCZ_12);
    spr.setTextColor(rgb(160, 160, 160));
    spr.drawString(b, CX, CY + 12);
  }
}

// ---------------- Obrazovka 1: radar ----------------
static void drawRadar(const PlaneData& pd, uint32_t now) {
  const int R = 116;
  const uint16_t ring = rgb(0, 90, 40);
  float range = RANGES_KM[gRangeIdx];

  // otáčející se paprsek
  float sweep = fmodf(now * 0.09f, 360.0f);  // 1 otáčka / 4 s
  spr.fillArc(CX, CY, R, 0, sweep - 90 - 45, sweep - 90, rgb(0, 40, 15));
  spr.fillArc(CX, CY, R, 0, sweep - 90 - 15, sweep - 90, rgb(0, 70, 25));

  for (int i = 1; i <= 3; i++) spr.drawCircle(CX, CY, R * i / 3, ring);
  spr.drawFastHLine(CX - R, CY, 2 * R, ring);
  spr.drawFastVLine(CX, CY - R, 2 * R, ring);
  float sa = sweep * DEG_TO_RAD;
  spr.drawLine(CX, CY, CX + R * sinf(sa), CY - R * cosf(sa), rgb(0, 200, 80));

  spr.setFont(&fonts::Font0);
  spr.setTextColor(rgb(0, 160, 70));
  spr.setTextDatum(textdatum_t::middle_left);
  char b[48];
  for (int i = 1; i <= 3; i++) {
    float km = range * i / 3;
    if (km >= 10) snprintf(b, sizeof b, "%.0f", km);
    else fmtDec(b, sizeof b, km, 1);
    spr.drawString(b, CX + 3, CY - R * i / 3 + 6);
  }
  spr.setTextDatum(textdatum_t::top_center);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(0, 220, 100));
  spr.drawString("S", CX, CY - R + 2);  // sever
  spr.fillCircle(CX, CY, 2, rgb(255, 255, 255));  // domov

  // letadla (od nejvzdálenějšího, aby nejbližší bylo navrchu)
  int shown = 0;
  for (int i = pd.count - 1; i >= 0; i--) {
    const Plane& pl = pd.p[i];
    if (pl.distKm > range) continue;
    shown++;
    float rr = pl.distKm / range * R, a = pl.brg * DEG_TO_RAD;
    int x = CX + (int)(rr * sinf(a)), y = CY - (int)(rr * cosf(a));
    uint16_t col = altColor(pl.alt);
    if (i == 0) spr.drawCircle(x, y, 10, rgb(255, 255, 255));
    drawPlaneIcon(x, y, pl.track, col);
    spr.setFont(&fonts::Font0);
    spr.setTextColor(col);
    spr.setTextDatum(x > CX + 60 ? textdatum_t::middle_right : textdatum_t::middle_left);
    spr.drawString(pl.flight, x > CX + 60 ? x - 9 : x + 9, y - 6);
  }

  // počet a nejbližší letadlo
  spr.setFont(&fontCZ_12);
  spr.setTextDatum(textdatum_t::top_center);
  spr.setTextColor(rgb(200, 200, 200));
  snprintf(b, sizeof b, "%d %s · %.0f km", shown, planesWord(shown), range);
  spr.drawString(b, CX, 24);

  if (pd.count > 0 && pd.p[0].distKm <= range) {
    const Plane& n = pd.p[0];
    char alt[16], d[12];
    altText(alt, sizeof alt, n.alt);
    fmtDec(d, sizeof d, n.distKm, 1);
    spr.fillRoundRect(CX - 70, 188, 140, 30, 6, rgb(0, 0, 0));
    spr.drawRoundRect(CX - 70, 188, 140, 30, 6, altColor(n.alt));
    spr.setTextDatum(textdatum_t::top_center);
    spr.setTextColor(rgb(255, 255, 255));
    spr.drawString(n.flight, CX, 189);
    snprintf(b, sizeof b, "%s km · %s", d, alt);
    spr.setTextColor(altColor(n.alt));
    spr.drawString(b, CX, 203);
  } else if (!pd.ok) {
    spr.setTextColor(rgb(255, 120, 0));
    spr.setTextDatum(textdatum_t::top_center);
    spr.drawString(pd.err[0] ? pd.err : "načítám…", CX, 196);
  }
}

// ---------------- Obrazovka 2: detail letadla ----------------
static int gDetailIdx = 0;

static void drawDetail(const PlaneData& pd) {
  float range = RANGES_KM[gRangeIdx];
  int n = 0;
  while (n < pd.count && pd.p[n].distKm <= range) n++;
  if (n == 0) {
    drawCenteredMsg(pd.ok ? "žádné letadlo" : "načítám…", pd.ok ? "v dosahu radaru" : pd.err);
    return;
  }
  if (gDetailIdx >= n) gDetailIdx = 0;
  const Plane& pl = pd.p[gDetailIdx];
  uint16_t col = altColor(pl.alt);
  char b[48], t[24];

  // obvod: směr k letadlu
  spr.drawCircle(CX, CY, 117, rgb(60, 60, 60));
  float a = pl.brg * DEG_TO_RAD;
  drawPlaneIcon(CX + (int)(108 * sinf(a)), CY - (int)(108 * cosf(a)), pl.track, col, 8);

  // logo společnosti (stahuje se na pozadí; dokud není, rozložení bez loga)
  static uint8_t logoPx[LOGO_SIZE * LOGO_SIZE];
  char icao[4];
  bool logo = logoIcaoFromCallsign(pl.flight, icao) && logoGet(icao, logoPx);
  // svislé pozice: index, volací znak, typ, výška, FL, rychlost, vzdálenost
  const int* Y;
  static const int Y_LOGO[] = {216, 88, 114, 138, 158, 178, 198};
  static const int Y_PLAIN[] = {30, 60, 90, 120, 142, 166, 188};
  Y = logo ? Y_LOGO : Y_PLAIN;
  if (logo) spr.pushImage(CX - LOGO_SIZE / 2, 16, LOGO_SIZE, LOGO_SIZE, (const lgfx::rgb332_t*)logoPx);

  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(150, 150, 150));
  snprintf(b, sizeof b, "%d / %d", gDetailIdx + 1, n);
  spr.drawString(b, CX, Y[0]);

  spr.setFont(&fonts::FreeSansBold18pt7b);
  spr.setTextColor(col);
  spr.drawString(pl.flight, CX, Y[1]);

  spr.setFont(&fontCZ_16);
  spr.setTextColor(rgb(200, 200, 200));
  snprintf(b, sizeof b, "%s  %s", pl.type[0] ? pl.type : "", pl.reg);
  spr.drawString(b, CX, Y[2]);

  // výška
  spr.setFont(&fontCZ_24);
  spr.setTextColor(rgb(255, 255, 255));
  altText(t, sizeof t, pl.alt);
  spr.drawString(t, CX, Y[3]);
  if (pl.alt != ALT_GROUND && pl.alt != ALT_UNKNOWN) {
    spr.setFont(&fontCZ_12);
    spr.setTextColor(rgb(150, 150, 150));
    snprintf(b, sizeof b, "%ld ft · FL%03ld", (long)pl.alt, lroundf(pl.alt / 100.0f));
    spr.drawString(b, CX, Y[4]);
    // stoupání / klesání
    if (abs(pl.vrate) > 300) {
      int x = CX + 80, y = Y[3];
      if (pl.vrate > 0) spr.fillTriangle(x, y - 8, x - 6, y + 4, x + 6, y + 4, rgb(0, 255, 80));
      else spr.fillTriangle(x, y + 8, x - 6, y - 4, x + 6, y - 4, rgb(255, 120, 0));
    }
  }

  // rychlost, kurz, vzdálenost
  spr.setFont(&fontCZ_16);
  spr.setTextColor(rgb(255, 230, 120));
  if (!isnan(pl.gs)) snprintf(b, sizeof b, "%ld km/h", lroundf(pl.gs * 1.852f));
  else strlcpy(b, "– km/h", sizeof b);
  if (!isnan(pl.track)) {
    size_t l = strlen(b);
    snprintf(b + l, sizeof b - l, " · %ld°", lroundf(pl.track));
  }
  spr.drawString(b, CX, Y[5]);
  fmtDec(t, sizeof t, pl.distKm, 1);
  snprintf(b, sizeof b, "%s km %s", t, compass8(pl.brg));
  spr.setTextColor(rgb(120, 200, 255));
  spr.drawString(b, CX, Y[6]);
}

// ---------------- Obrazovka 3: počasí ----------------
static void drawWeather(const Weather& w) {
  if (!w.ok) {
    drawCenteredMsg("ČHMI", w.err[0] ? w.err : (timeValid() ? "načítám…" : "čekám na čas (NTP)…"));
    return;
  }
  char b[48], t[16];
  uint16_t tc = tempColor(w.t);

  // oblouk teploty -20..+40 °C
  spr.fillArc(CX, CY, 118, 112, 135, 405, rgb(40, 40, 40));
  if (!isnan(w.t)) {
    float frac = constrain((w.t + 20) / 60.0f, 0.0f, 1.0f);
    spr.fillArc(CX, CY, 118, 112, 135, 135 + 270 * frac, tc);
  }

  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(150, 150, 150));
  snprintf(b, sizeof b, "ČHMI · %s", gCfg.name.c_str());
  spr.drawString(b, CX, 32);

  // teplota
  if (!isnan(w.t)) fmtDec(t, sizeof t, w.t, 1);
  else strlcpy(t, "--", sizeof t);
  spr.setFont(&fonts::FreeSansBold24pt7b);
  spr.setTextColor(tc);
  int tw = spr.textWidth(t);
  spr.setTextDatum(textdatum_t::middle_left);
  int x0 = CX - (tw + 26) / 2;
  spr.drawString(t, x0, 72);
  spr.drawCircle(x0 + tw + 6, 58, 4, tc);
  spr.drawCircle(x0 + tw + 6, 58, 3, tc);
  spr.setFont(&fonts::FreeSansBold12pt7b);
  spr.drawString("C", x0 + tw + 11, 66);

  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_16);
  int y = 110;
  if (!isnan(w.h)) {
    snprintf(b, sizeof b, "vlhkost %.0f %%", w.h);
    spr.setTextColor(rgb(120, 200, 255));
    spr.drawString(b, CX, y);
    y += 22;
  }
  if (!isnan(w.p)) {
    float p = w.p;
    float tt = isnan(w.t) ? 10 : w.t;
    p = w.p * powf(1 - 0.0065f * gCfg.elev / (tt + 0.0065f * gCfg.elev + 273.15f), -5.257f);
    snprintf(b, sizeof b, "tlak %.0f hPa", p);
    spr.setTextColor(rgb(200, 200, 200));
    spr.drawString(b, CX, y);
    y += 22;
  }
  if (!isnan(w.f)) {
    fmtDec(t, sizeof t, w.f, 1);
    if (!isnan(w.fmax)) {
      char g[12];
      fmtDec(g, sizeof g, w.fmax, 0);
      snprintf(b, sizeof b, "vítr %s (%s) m/s", t, g);
    } else {
      snprintf(b, sizeof b, "vítr %s m/s", t);
    }
    if (!isnan(w.d) && w.f > 0.2f) {
      size_t l = strlen(b);
      snprintf(b + l, sizeof b - l, " %s", compass8(w.d));
    }
    spr.setTextColor(rgb(160, 255, 160));
    spr.drawString(b, CX, y);
    y += 22;
  }
  if (!isnan(w.rain)) {
    fmtDec(t, sizeof t, w.rain, 1);
    snprintf(b, sizeof b, "srážky dnes %s mm", t);
    spr.setTextColor(rgb(100, 150, 255));
    spr.drawString(b, CX, y);
  }

  if (w.obs) {
    struct tm lt;
    localtime_r(&w.obs, &lt);
    snprintf(b, sizeof b, "měřeno %02d:%02d", lt.tm_hour, lt.tm_min);
    spr.setFont(&fontCZ_12);
    spr.setTextColor(rgb(120, 120, 120));
    spr.drawString(b, CX, 208);
  }
}

// ---------------- Obrazovka 4: hodiny ----------------
static void drawClock(const PlaneData& pd, const Weather& w, const Forecast& fc) {
  if (!timeValid()) {
    drawCenteredMsg("čekám na čas…");
    return;
  }
  static const char* DNY[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  char b[48];

  // sekundový oblouk
  spr.fillArc(CX, CY, 118, 114, -90, 270, rgb(30, 30, 30));
  spr.fillArc(CX, CY, 118, 114, -90, -90 + lt.tm_sec * 6, rgb(0, 160, 255));

  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fonts::Font7);
  spr.setTextColor(rgb(255, 255, 255));
  snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
  spr.drawString(b, CX, CY - 10);

  spr.setFont(&fontCZ_16);
  spr.setTextColor(rgb(180, 180, 180));
  snprintf(b, sizeof b, "%s %d. %d.", DNY[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1);
  spr.drawString(b, CX, CY + 32);

  float temp = (w.ok && !isnan(w.t)) ? w.t : (fc.ok ? fc.curT : NAN);
  if (!isnan(temp)) {
    char t[12];
    fmtDec(t, sizeof t, temp, 1);
    snprintf(b, sizeof b, "%s °C", t);
    spr.setTextColor(tempColor(temp));
    spr.drawString(b, CX - 40, CY + 62);
  }
  float range = RANGES_KM[gRangeIdx];
  int n = 0;
  while (n < pd.count && pd.p[n].distKm <= range) n++;
  snprintf(b, sizeof b, "%d", n);
  spr.setTextColor(rgb(0, 220, 100));
  spr.drawString(b, CX + 32, CY + 62);
  drawPlaneIcon(CX + 32 + spr.textWidth(b) / 2 + 12, CY + 62, 45, rgb(0, 220, 100), 7);
  if (WiFi.status() == WL_CONNECTED) {  // adresa webového nastavení
    spr.setFont(&fontCZ_12);
    spr.setTextColor(rgb(90, 90, 90));
    spr.drawString(WiFi.localIP().toString().c_str(), CX, CY + 88);
  }
}

// ===================================================================
//  Tlačítko a hlavní smyčka
// ===================================================================
// ---------------- Ikony počasí ----------------
static void drawCloud(int x, int y, int s, uint16_t col) {
  spr.fillCircle(x - s / 4, y + s / 16, s / 5, col);
  spr.fillCircle(x + s / 12, y - s / 10, s / 4, col);
  spr.fillCircle(x + s / 3, y + s / 12, s / 6, col);
  spr.fillRoundRect(x - s / 2 + s / 16, y + s / 16, s - s / 8, s / 5, s / 10, col);
}

static void drawWxIcon(int x, int y, int code, bool day, int s) {
  const uint16_t SUN = rgb(255, 200, 0), MOON = rgb(230, 230, 190), CL = rgb(200, 206, 216),
                 DK = rgb(130, 136, 150), RAIN = rgb(80, 150, 255), SNOW = rgb(240, 245, 255), BOLT = rgb(255, 225, 0);
  bool clear = code <= 2;
  if (clear) {  // slunce / měsíc (u polojasna menší a posunuté)
    int ox = code ? x - s / 5 : x, oy = code ? y - s / 6 : y, r = code ? s / 5 : s / 4;
    if (day) {
      spr.fillCircle(ox, oy, r, SUN);
      for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG_TO_RAD;
        spr.drawLine(ox + cosf(rad) * (r + 3), oy + sinf(rad) * (r + 3), ox + cosf(rad) * (r + s / 7),
                     oy + sinf(rad) * (r + s / 7), SUN);
      }
    } else {
      spr.fillCircle(ox, oy, r, MOON);
      spr.fillCircle(ox + r / 2, oy - r / 3, r, TFT_BLACK);
    }
    if (code) drawCloud(x + s / 8, y + s / 8, s * 3 / 4, CL);
    return;
  }
  if (code == 45 || code == 48) {  // mlha
    for (int i = 0; i < 4; i++) spr.fillRoundRect(x - s / 2 + (i & 1) * 4, y - s / 4 + i * s / 6, s - 8, 3, 1, DK);
    return;
  }
  bool storm = code >= 95;
  drawCloud(x, y - s / 8, s, storm ? DK : CL);
  int by = y + s / 5;
  if (storm) {
    spr.fillTriangle(x + 2, by, x - 6, by + s / 4, x + 1, by + s / 4, BOLT);
    spr.fillTriangle(x + 1, by + s / 5, x - 2, by + s / 2, x + 7, by + s / 6, BOLT);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    for (int i = -1; i <= 1; i++) {
      spr.fillCircle(x + i * s / 4, by + s / 8 + (i & 1) * 5, 2, SNOW);
      spr.fillCircle(x + i * s / 4 + s / 8, by + s / 4 + (i & 1) * 3, 2, SNOW);
    }
  } else if (code >= 51) {
    int drops = (code == 65 || code == 82 || code == 67) ? 4 : (code >= 61 ? 3 : 2);
    for (int i = 0; i < drops; i++) {
      int dx = x - s / 3 + i * (2 * s / 3) / max(1, drops - 1);
      spr.drawLine(dx, by + 2, dx - 4, by + s / 4, RAIN);
      spr.drawLine(dx + 1, by + 2, dx - 3, by + s / 4, RAIN);
    }
  }
}

static uint16_t alertColor(uint8_t lv) {
  return lv == AL_DANGER ? rgb(255, 80, 60) : lv == AL_WARN ? rgb(255, 180, 0) : rgb(120, 200, 255);
}

// ---------------- Obrazovka: předpověď ----------------
static void drawForecast(const Forecast& f) {
  if (!f.ok) {
    drawCenteredMsg("Předpověď", f.err[0] ? f.err : "načítám…");
    return;
  }
  char b[64], t[16];
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(150, 150, 150));
  spr.drawString("Předpověď", CX, 22);

  drawWxIcon(84, 62, f.curCode, f.isDay, 46);
  fmtDec(t, sizeof t, f.curT, 1);
  spr.setFont(&fonts::FreeSansBold18pt7b);
  spr.setTextColor(tempColor(f.curT));
  spr.setTextDatum(textdatum_t::middle_left);
  int tw = spr.textWidth(t);
  spr.drawString(t, 116, 58);
  spr.drawCircle(116 + tw + 5, 47, 3, tempColor(f.curT));
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(200, 200, 200));
  if (!isnan(f.curGust) && f.curGust >= 30)
    snprintf(b, sizeof b, "%s · nárazy %.0f km/h", weatherText(f.curCode), f.curGust);
  else
    snprintf(b, sizeof b, "%s", weatherText(f.curCode));
  spr.drawString(b, CX, 92);

  // upozornění
  int y = 112;
  if (f.nAlerts == 0) {
    spr.setTextColor(rgb(80, 220, 120));
    spr.drawString("bez výrazných jevů", CX, y + 10);
  }
  for (int i = 0; i < f.nAlerts && i < 3; i++, y += 17) {
    uint16_t col = alertColor(f.alertLevel[i]);
    int w = spr.textWidth(f.alert[i]);
    spr.fillCircle(CX - w / 2 - 7, y, 3, col);
    spr.setTextColor(col);
    spr.drawString(f.alert[i], CX + 3, y);
  }

  // hodinový pruh: za 2, 4, 6, 8 h
  spr.drawFastHLine(48, 164, 144, rgb(50, 50, 50));
  for (int k = 0; k < 4; k++) {
    int i = 2 + k * 2;
    if (i >= f.nh) break;
    int x = 69 + k * 34;
    snprintf(b, sizeof b, "%d:00", f.hHour[i]);
    spr.setTextColor(rgb(140, 140, 140));
    spr.drawString(b, x, 176);
    fmtDec(t, sizeof t, f.hT[i], 0);
    snprintf(b, sizeof b, "%s°", t);
    spr.setTextColor(tempColor(f.hT[i]));
    spr.drawString(b, x, 192);
    // srážky: sloupek podle pravděpodobnosti
    int ph = f.hProb[i] * 12 / 100;
    spr.drawRect(x - 9, 202, 18, 13, rgb(50, 50, 60));
    if (ph) spr.fillRect(x - 8, 214 - ph, 16, ph, f.hPrec[i] >= 0.1f ? rgb(70, 140, 255) : rgb(40, 70, 120));
  }
}

// ---------------- Obrazovka: meteoradar ----------------
static void rainRun(int x, int y, int len, uint8_t c, void*) { spr.fillRect(x * 2, y * 2, len * 2, 2, c); }

static void drawRain(const Forecast& fc, uint32_t now) {
  int n = radarFrameCount();
  float R = RAIN_RADII[constrain(gCfg.rainRadiusIdx, 0, 3)];
  char b[64];
  if (n == 0) {
    drawCenteredMsg("Meteoradar ČHMI", radarError()[0] ? radarError() : "načítám snímky…");
    return;
  }
  // animace: snímky po 600 ms, poslední drží 2,4 s
  const uint32_t STEP = 600, HOLD = 2400;
  uint32_t cycle = (n - 1) * STEP + HOLD, t = now % cycle;
  int idx = min(n - 1, (int)(t / STEP));
  radarForEachRun(idx, rainRun, nullptr);

  // překryv: kruhy, domov, sever
  const uint16_t ring = rgb(90, 90, 90);
  spr.drawCircle(CX, CY, 60, ring);
  spr.drawCircle(CX, CY, 118, ring);
  spr.drawFastHLine(CX - 6, CY, 13, rgb(255, 255, 255));
  spr.drawFastVLine(CX, CY - 6, 13, rgb(255, 255, 255));
  spr.setFont(&fonts::Font0);
  spr.setTextColor(rgb(160, 160, 160));
  spr.setTextDatum(textdatum_t::middle_left);
  snprintf(b, sizeof b, "%.0f", R / 2);
  spr.drawString(b, CX + 3, CY - 56);
  snprintf(b, sizeof b, "%.0f km", R);
  spr.drawString(b, CX + 3, CY - 112);

  // čas snímku a průběh animace
  time_t ft = radarFrameTime(idx);
  struct tm lt;
  localtime_r(&ft, &lt);
  int ago = timeValid() ? (int)((time(nullptr) - ft) / 60) : -1;
  spr.fillRoundRect(CX - 52, 14, 104, 30, 6, TFT_BLACK);
  spr.setFont(&fontCZ_16);
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setTextColor(idx == n - 1 ? rgb(255, 255, 255) : rgb(170, 170, 170));
  snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
  spr.drawString(b, CX, 24);
  for (int i = 0; i < n; i++)
    spr.fillCircle(CX - (n - 1) * 5 + i * 10, 38, i == idx ? 3 : 2, i == idx ? rgb(255, 255, 255) : rgb(90, 90, 90));

  // dole: věta o srážkách z předpovědi
  const char* msg = fc.ok && fc.nowcast[0] ? fc.nowcast : (fc.ok ? "v příštích 2 h bez srážek" : "");
  if (ago >= 0 && idx == n - 1 && ago > 20) {
    snprintf(b, sizeof b, "snímek je %d min starý", ago);
    msg = b;
  }
  if (msg[0]) {
    spr.setFont(&fontCZ_12);
    int w = spr.textWidth(msg) + 16;
    spr.fillRoundRect(CX - w / 2, 192, w, 22, 6, TFT_BLACK);
    spr.setTextColor(fc.nowcast[0] ? rgb(255, 190, 60) : rgb(120, 220, 140));
    spr.drawString(msg, CX, 203);
  }
}

// ===================================================================
//  Jas displeje
// ===================================================================
// S pinem podsvícení (BL na GPIO) se jas řídí PWM. Bez něj se ztmavuje obraz:
// snímek (RGB332) se při odesílání převádí přes tabulku 256 barev se započteným jasem.
static uint16_t gLut[256];
static int gLutPct = -1;
static int gCurPct = 100;
static uint32_t gBoostUntil = 0, gPreviewUntil = 0;
static int gPreviewPct = 100;
static const char* gPeriod = "den";
static bool gPwm = false;
static const int BL_CH = 0;

static bool until(uint32_t t) { return (int32_t)(t - millis()) > 0; }

static void buildLut(int pct) {
  float f = pct / 100.0f;
  for (int v = 0; v < 256; v++) {
    int r = ((v >> 5) & 7) * 255 / 7, g = ((v >> 2) & 7) * 255 / 7, b = (v & 3) * 85;
    gLut[v] = lgfx::color565(lroundf(r * f), lroundf(g * f), lroundf(b * f));
  }
  gLutPct = pct;
}

static void pushFrame() {
  int soft = gPwm ? 100 : gCurPct;
  if (soft != gLutPct) buildLut(soft);
  static uint16_t line[240 * 12];
  const uint8_t* src = (const uint8_t*)spr.getBuffer();
  if (!src) return;
  tft.startWrite();
  for (int y = 0; y < 240; y += 12) {
    const uint8_t* row = src + y * 240;
    for (int i = 0; i < 240 * 12; i++) line[i] = gLut[row[i]];
    tft.setAddrWindow(0, y, 240, 12);
    tft.writePixels(line, 240 * 12, true);
  }
  tft.endWrite();
}

// jas podle denní doby
static int scheduledPct() {
  if (!timeValid()) {
    gPeriod = "den";
    return gCfg.bDay;
  }
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min;
  struct P {
    int start, pct;
    const char* name;
  } p[3] = {{gCfg.tDay, gCfg.bDay, "den"}, {gCfg.tEve, gCfg.bEve, "večer"}, {gCfg.tNight, gCfg.bNight, "noc"}};
  // seřadit podle začátku; před prvním začátkem platí poslední období (přes půlnoc)
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2 - i; j++)
      if (p[j].start > p[j + 1].start) std::swap(p[j], p[j + 1]);
  int cur = 2;
  for (int i = 0; i < 3; i++)
    if (m >= p[i].start) cur = i;
  gPeriod = p[cur].name;
  return p[cur].pct;
}

static void updateBrightness() {
  int pct;
  if (gPortalMode) pct = 100;
  else if (until(gPreviewUntil)) pct = gPreviewPct;
  else {
    pct = scheduledPct();
    if (until(gBoostUntil)) pct = max(pct, max(gCfg.bDay, 60));
  }
  pct = constrain(pct, 0, 100);
  if (pct == gCurPct && gLutPct >= 0) return;
  gCurPct = pct;
  if (gPwm) {
    float f = pct / 100.0f;
    int duty = pct == 0 ? 0 : max(1, (int)(1023 * f * f));  // kvadraticky ~ vnímaný jas
    ledcWrite(BL_CH, duty);
  }
}

static void brightnessBegin() {
  if (gCfg.blPin >= 0) {
    ledcSetup(BL_CH, 5000, 10);
    ledcAttachPin(gCfg.blPin, BL_CH);
    ledcWrite(BL_CH, 1023);
    gPwm = true;
  }
  buildLut(100);
}

// v ztlumeném režimu první stisk tlačítka jen rozsvítí displej na 30 s
static bool wakeOnPress() {
  bool dim = !gPortalMode && !until(gBoostUntil) && scheduledPct() < 30;
  if (until(gBoostUntil) || dim) gBoostUntil = millis() + 30000;
  return dim;
}

void brightnessPreview(int pct) {
  gPreviewPct = constrain(pct, 0, 100);
  gPreviewUntil = millis() + 4000;
}
int brightnessCurrent() { return gCurPct; }
const char* brightnessPeriod() { return until(gPreviewUntil) ? "náhled" : until(gBoostUntil) ? "probuzeno tlačítkem" : gPeriod; }

// ===================================================================
//  Stav aplikace pro stránku /system
// ===================================================================
static TaskHandle_t gNetTask = nullptr, gLoopTask = nullptr;

void appStatusJson(JsonObject o) {
  float range = RANGES_KM[gRangeIdx];
  xSemaphoreTake(gMutex, portMAX_DELAY);
  int n = 0;
  while (n < gPlanes.count && gPlanes.p[n].distKm <= range) n++;
  long pAge = gPlanes.updatedMs ? (long)((millis() - gPlanes.updatedMs) / 1000) : -1;
  long wAge = gWeather.updatedMs ? (long)((millis() - gWeather.updatedMs) / 1000) : -1;
  String pErr = gPlanes.err, wErr = gWeather.err;
  xSemaphoreGive(gMutex);

  o["planes"] = n;
  o["range"] = (int)range;
  o["planes_age"] = gPortalMode ? -1 : pAge;
  o["planes_err"] = pErr;
  o["weather_age"] = gPortalMode ? -1 : wAge;
  o["weather_err"] = wErr;
  o["station"] = gCfg.name + " (" + gCfg.wsi + ")";
  static const char* const OTA_TXT[] = {"nekontrolováno", "kontroluji…", "aktuální verze",
                                        "je dostupná nová verze", "stahuji…", "chyba"};
  String ota = OTA_TXT[gOta.state];
  if (gOta.latest[0]) ota += String(" (GitHub: ") + gOta.latest + ")";
  if (gOta.state == OTA_FAILED && gOta.msg[0]) ota += String(": ") + gOta.msg;
  o["ota"] = ota;
  {
    Forecast f;
    forecastGet(f);
    o["fc_age"] = f.updatedMs ? (long)((millis() - f.updatedMs) / 1000) : -1;
    o["fc_err"] = f.err;
    o["fc_alerts"] = f.nAlerts;
  }
  o["rain_frames"] = radarFrameCount();
  o["rain_age"] = radarUpdatedMs() ? (long)((millis() - radarUpdatedMs()) / 1000) : -1;
  o["rain_err"] = radarError();
  o["bright"] = gCurPct;
  o["period"] = brightnessPeriod();
  o["backlight"] = gPwm ? "PWM na GPIO " + String(gCfg.blPin) : String("softwarové stmívání obrazu");
  o["stack_net"] = gNetTask ? uxTaskGetStackHighWaterMark(gNetTask) : 0;
  o["stack_loop"] = gLoopTask ? uxTaskGetStackHighWaterMark(gLoopTask) : 0;
}

static int gScreen = SCR_RADAR;
static uint32_t gBtnHeldMs = 0;  // jak dlouho je tlačítko drženo (pro indikaci)
static const uint32_t PORTAL_HOLD_MS = 5000;

static void onShortPress() {
  if (gPortalMode) {  // v portálu: krátký stisk = zpět do normálního režimu
    if (settingsHasWifi()) ESP.restart();
    return;
  }
  for (int k = 1; k <= SCR_COUNT; k++) {
    int sc = (gScreen + k) % SCR_COUNT;
    if (scrOn(sc)) {
      gScreen = sc;
      break;
    }
  }
  gDetailIdx = 0;
}

static void onLongPress() {
  if (gPortalMode) return;
  switch (gScreen) {
    case SCR_RADAR:
      gRangeIdx = (gRangeIdx + 1) % NUM_RANGES;
      gCfg.rangeIdx = gRangeIdx;
      settingsSaveRange();
      gForcePlanes = true;
      break;
    case SCR_DETAIL:
      gDetailIdx++;
      break;
    case SCR_RAIN:
      gCfg.rainRadiusIdx = (gCfg.rainRadiusIdx + 1) % 4;
      settingsSave();
      gForceRain = true;
      break;
    case SCR_FORECAST:
      gForceForecast = true;
      break;
    default:
      gForceWeather = true;
      gForcePlanes = true;
      gForceForecast = true;
      break;
  }
}

// krátký stisk < 0,7 s, dlouhý 0,7–5 s (akce po puštění), držení 5 s = konfigurační hotspot
static void pollButton() {
  static bool down = false, swallow = false;
  static uint32_t downAt = 0, lastChange = 0;
  bool pressed = digitalRead(PIN_BUTTON) == LOW;
  uint32_t t = millis();
  if (down) gBtnHeldMs = t - downAt;
  if (t - lastChange < 30) return;  // debounce
  if (pressed && !down) {
    down = true;
    downAt = lastChange = t;
    swallow = wakeOnPress();
  } else if (pressed && down && !gPortalMode && t - downAt >= PORTAL_HOLD_MS) {
    requestPortal();
  } else if (!pressed && down) {
    down = false;
    gBtnHeldMs = 0;
    lastChange = t;
    uint32_t held = t - downAt;
    if (swallow) return;  // stisk jen probudil displej
    if (held < 700) onShortPress();
    else onLongPress();
  }
}

static void drawHoldIndicator() {
  if (gBtnHeldMs < 1200 || gPortalMode) return;
  float frac = min(1.0f, (float)gBtnHeldMs / PORTAL_HOLD_MS);
  spr.fillArc(CX, CY, 119, 113, -90, -90 + 360 * frac, rgb(255, 160, 0));
  spr.fillRoundRect(CX - 80, CY - 16, 160, 32, 8, rgb(0, 0, 0));
  spr.setFont(&fontCZ_12);
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setTextColor(rgb(255, 160, 0));
  spr.drawString("drž pro nastavení WiFi", CX, CY);
}

static void drawPortal() {
  char b[64];
  String ap = apName();
  bool client = WiFi.softAPgetStationNum() > 0;
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_16);
  spr.setTextColor(rgb(255, 160, 0));
  spr.drawString("Nastavení", CX, 28);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(200, 200, 200));
  if (!client) {
    // QR kód pro připojení k hotspotu (otevřená síť)
    snprintf(b, sizeof b, "WIFI:T:nopass;S:%s;;", ap.c_str());
    spr.qrcode(b, CX - 56, 44, 112, 3);
    spr.drawString("1. připoj se k WiFi", CX, 170);
    spr.setTextColor(rgb(255, 255, 255));
    spr.drawString(ap.c_str(), CX, 186);
  } else {
    spr.qrcode("http://192.168.4.1/", CX - 56, 44, 112, 3);
    spr.drawString("2. otevři v prohlížeči", CX, 170);
    spr.setTextColor(rgb(255, 255, 255));
    spr.drawString("192.168.4.1", CX, 186);
  }
  spr.setTextColor(rgb(110, 110, 110));
  spr.drawString(settingsHasWifi() ? "stiskem tlačítka zpět" : "verze " FW_VERSION, CX, 206);
}

static void drawOta() {
  char b[48];
  spr.fillArc(CX, CY, 118, 110, -90, 270, rgb(40, 40, 40));
  spr.fillArc(CX, CY, 118, 110, -90, -90 + 360 * gOta.progress / 100, rgb(0, 160, 255));
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setFont(&fontCZ_16);
  spr.setTextColor(rgb(255, 255, 255));
  spr.drawString("Aktualizace", CX, CY - 30);
  spr.setFont(&fontCZ_24);
  snprintf(b, sizeof b, "%d %%", gOta.progress);
  spr.drawString(b, CX, CY + 2);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(160, 160, 160));
  snprintf(b, sizeof b, "%s → %s", FW_VERSION, gOta.latest);
  spr.drawString(b, CX, CY + 32);
  spr.drawString("nevypínej napájení", CX, CY + 50);
}

static void showBoot(const char* a, const char* b) {
  spr.fillScreen(TFT_BLACK);
  drawCenteredMsg(a, b);
  spr.setFont(&fontCZ_12);
  spr.setTextColor(rgb(90, 90, 90));
  spr.drawString("verze " FW_VERSION, CX, 206);
  pushFrame();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Serial.printf("\nesp32-stand-system %s\n", FW_VERSION);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(8);  // 240x240x8 bit = 57 kB (16 bit by nenechalo dost RAM pro TLS)
  if (!spr.createSprite(240, 240)) Serial.println("Sprite: málo paměti!");

  settingsLoad();
  brightnessBegin();
  gRangeIdx = constrain(gCfg.rangeIdx, 0, NUM_RANGES - 1);
  gMutex = xSemaphoreCreateMutex();
  gLoopTask = xTaskGetCurrentTaskHandle();
  sysinfoBegin();

  bool portalFlag = settingsTakePortalFlag();
  if (!settingsHasWifi() || portalFlag) {
    gPortalMode = true;
    showBoot("Spouštím hotspot…", nullptr);
    webBegin(true);
    return;
  }

  if (!scrOn(gScreen)) onShortPress();
  showBoot("Připojuji WiFi…", gCfg.ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("letadla");
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  webBegin(false);  // nastavení i v domácí síti na IP zařízení
  xTaskCreate(netTask, "net", 16384, nullptr, 1, &gNetTask);
}

void loop() {
  static PlaneData pd;
  static Weather w;
  static Forecast fc;
  static uint32_t lastDraw = 0;

  pollButton();
  webLoop();
  uint32_t now = millis();

  if (gPortalMode && settingsHasWifi() && now - webLastActivity() > PORTAL_IDLE_RESTART_MS &&
      WiFi.softAPgetStationNum() == 0) {
    ESP.restart();  // nikdo nic nenastavuje -> zkus znovu uloženou WiFi
  }

  uint32_t frame = (gScreen == SCR_RADAR || gBtnHeldMs) ? 66 : 250;
  if (now - lastDraw < frame) {
    delay(5);
    return;
  }
  lastDraw = now;
  updateBrightness();

  spr.fillScreen(TFT_BLACK);
  if (gPortalMode) {
    drawPortal();
  } else if (gOta.state == OTA_DOWNLOADING) {
    drawOta();
  } else {
    xSemaphoreTake(gMutex, portMAX_DELAY);
    pd = gPlanes;
    w = gWeather;
    xSemaphoreGive(gMutex);
    forecastGet(fc);
    switch (gScreen) {
      case SCR_RADAR: drawRadar(pd, now); break;
      case SCR_DETAIL: drawDetail(pd); break;
      case SCR_WEATHER: drawWeather(w); break;
      case SCR_CLOCK: drawClock(pd, w, fc); break;
      case SCR_FORECAST: drawForecast(fc); break;
      case SCR_RAIN: drawRain(fc, now); break;
    }
    drawWifiDot();
    drawHoldIndicator();
  }
  pushFrame();
}
