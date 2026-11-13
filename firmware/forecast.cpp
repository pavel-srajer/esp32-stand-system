#include "forecast.h"
#include "settings.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

static Forecast sFc;
static SemaphoreHandle_t sMutex = nullptr;

static void lock() {
  if (!sMutex) sMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(sMutex, portMAX_DELAY);
}

void forecastGet(Forecast& out) {
  lock();
  out = sFc;
  xSemaphoreGive(sMutex);
}

const char* weatherText(int c) {
  switch (c) {
    case 0: return "jasno";
    case 1: return "skoro jasno";
    case 2: return "polojasno";
    case 3: return "zataženo";
    case 45: case 48: return "mlha";
    case 51: case 53: case 55: return "mrholení";
    case 56: case 57: return "mrznoucí mrholení";
    case 61: return "slabý déšť";
    case 63: return "déšť";
    case 65: return "silný déšť";
    case 66: case 67: return "mrznoucí déšť";
    case 71: return "slabé sněžení";
    case 73: return "sněžení";
    case 75: return "silné sněžení";
    case 77: return "sněhová zrna";
    case 80: case 81: return "přeháňky";
    case 82: return "silné přeháňky";
    case 85: case 86: return "sněhové přeháňky";
    case 95: return "bouřka";
    case 96: case 99: return "bouřka s kroupami";
    default: return "";
  }
}

static bool isSnow(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
static bool isFreezing(int c) { return c == 56 || c == 57 || c == 66 || c == 67; }

static void addAlert(Forecast& f, AlertLevel lv, const char* fmt, ...) {
  if (f.nAlerts >= 4) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(f.alert[f.nAlerts], sizeof f.alert[0], fmt, ap);
  va_end(ap);
  f.alertLevel[f.nAlerts++] = lv;
}

static void computeAlerts(Forecast& f) {
  f.nAlerts = 0;
  f.nowcast[0] = 0;
  const float WET = 0.1f;  // mm / 15 min
  bool cold = !isnan(f.curT) && f.curT <= 1.0f;
  const char* what = cold ? "sněžit" : "pršet";

  // 1) srážky v nejbližších 2 h (15min krok)
  if (f.nm15 > 0) {
    if (f.m15[0] >= WET) {
      int stop = -1;
      for (int i = 1; i < f.nm15; i++)
        if (f.m15[i] < WET) { stop = i; break; }
      if (stop > 0) snprintf(f.nowcast, sizeof f.nowcast, "%s, přestane asi za %d min", cold ? "Sněží" : "Prší", stop * 15);
      else snprintf(f.nowcast, sizeof f.nowcast, "%s, další 2 h nepřestane", cold ? "Sněží" : "Prší");
    } else {
      for (int i = 1; i < f.nm15; i++)
        if (f.m15[i] >= WET) {
          snprintf(f.nowcast, sizeof f.nowcast, "Začne %s asi za %d min", what, i * 15);
          break;
        }
    }
  }
  if (f.nowcast[0]) addAlert(f, AL_WARN, "%s", f.nowcast);

  int h12 = min(f.nh, 12), h24 = min(f.nh, 24);
  // 2) bouřky / mrznoucí srážky / sníh v příštích 12 h
  for (int i = 0; i < h12; i++)
    if (f.hCode[i] >= 95) {
      addAlert(f, AL_DANGER, "Bouřky kolem %d:00", f.hHour[i]);
      break;
    }
  for (int i = 0; i < h12; i++)
    if (isFreezing(f.hCode[i]) || (f.hPrec[i] >= 0.1f && f.hT[i] <= 0.5f && f.hT[i] >= -3)) {
      addAlert(f, AL_DANGER, "Náledí – mrznoucí srážky kolem %d:00", f.hHour[i]);
      break;
    }
  for (int i = 0; i < h12; i++)
    if (isSnow(f.hCode[i])) {
      addAlert(f, AL_WARN, "Sněžení kolem %d:00", f.hHour[i]);
      break;
    }
  // 3) déšť později (když ještě nehlásí nowcast)
  if (!f.nowcast[0]) {
    for (int i = 1; i < h12; i++)
      if (f.hPrec[i] >= 0.3f || f.hProb[i] >= 60) {
        addAlert(f, AL_INFO, "Srážky kolem %d:00 (%d %%)", f.hHour[i], f.hProb[i]);
        break;
      }
  }
  // 4) vítr
  float gMax = 0;
  int gAt = 0;
  for (int i = 0; i < h12; i++)
    if (f.hGust[i] > gMax) { gMax = f.hGust[i]; gAt = f.hHour[i]; }
  if (gMax >= 75) addAlert(f, AL_DANGER, "Velmi silný vítr, nárazy %.0f km/h", gMax);
  else if (gMax >= 50) addAlert(f, AL_WARN, "Silný vítr, nárazy %.0f km/h (%d:00)", gMax, gAt);
  // 5) mráz / horko v příštích 24 h
  float tMin = 99, tMax = -99;
  int minAt = 0, maxAt = 0;
  for (int i = 0; i < h24; i++) {
    if (f.hT[i] < tMin) { tMin = f.hT[i]; minAt = f.hHour[i]; }
    if (f.hT[i] > tMax) { tMax = f.hT[i]; maxAt = f.hHour[i]; }
  }
  if (tMin <= -10) addAlert(f, AL_DANGER, "Silný mráz až %.0f °C (%d:00)", tMin, minAt);
  else if (tMin <= 0) addAlert(f, AL_INFO, "Mráz až %.0f °C (%d:00)", tMin, minAt);
  if (tMax >= 33) addAlert(f, AL_DANGER, "Velké horko až %.0f °C", tMax);
  else if (tMax >= 30) addAlert(f, AL_WARN, "Horko až %.0f °C (%d:00)", tMax, maxAt);
}

bool forecastUpdate() {
  char url[512];
  snprintf(url, sizeof url,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,wind_speed_10m,wind_gusts_10m,is_day"
           "&minutely_15=precipitation&forecast_minutely_15=8"
           "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,wind_gusts_10m"
           "&forecast_hours=24"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum&forecast_days=3"
           "&timezone=Europe%%2FPrague&wind_speed_unit=kmh",
           gCfg.lat, gCfg.lon);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
  Forecast f;
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    lock();
    snprintf(sFc.err, sizeof sFc.err, "Open-Meteo HTTP %d", code);
    xSemaphoreGive(sMutex);
    Serial.printf("[Forecast] HTTP %d\n", code);
    return false;
  }
  BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  http.end();
  if (err) {
    lock();
    snprintf(sFc.err, sizeof sFc.err, "JSON %s", err.c_str());
    xSemaphoreGive(sMutex);
    return false;
  }

  JsonObject cur = doc["current"];
  f.curT = cur["temperature_2m"] | NAN;
  f.curCode = cur["weather_code"] | -1;
  f.curWind = cur["wind_speed_10m"] | NAN;
  f.curGust = cur["wind_gusts_10m"] | NAN;
  f.isDay = (cur["is_day"] | 1) != 0;

  JsonObject h = doc["hourly"];
  JsonArray ht = h["time"];
  f.nh = min((int)ht.size(), (int)Forecast::H);
  for (int i = 0; i < f.nh; i++) {
    const char* t = ht[i] | "";  // "2026-11-13T17:00"
    f.hHour[i] = strlen(t) >= 13 ? atoi(t + 11) : 0;
    f.hT[i] = h["temperature_2m"][i] | NAN;
    f.hPrec[i] = h["precipitation"][i] | 0.0f;
    f.hProb[i] = h["precipitation_probability"][i] | 0;
    f.hCode[i] = h["weather_code"][i] | 0;
    f.hGust[i] = h["wind_gusts_10m"][i] | 0.0f;
  }
  JsonArray m = doc["minutely_15"]["precipitation"];
  f.nm15 = min((int)m.size(), 8);
  for (int i = 0; i < f.nm15; i++) f.m15[i] = m[i] | 0.0f;

  JsonObject d = doc["daily"];
  f.nd = min((int)d["time"].size(), 3);
  for (int i = 0; i < f.nd; i++) {
    f.dCode[i] = d["weather_code"][i] | 0;
    f.dMax[i] = d["temperature_2m_max"][i] | NAN;
    f.dMin[i] = d["temperature_2m_min"][i] | NAN;
    f.dPrec[i] = d["precipitation_sum"][i] | 0.0f;
  }

  computeAlerts(f);
  f.ok = true;
  f.updatedMs = millis();
  Serial.printf("[Forecast] %.1f °C, %s, %d upozornění\n", f.curT, weatherText(f.curCode), f.nAlerts);
  for (int i = 0; i < f.nAlerts; i++) Serial.printf("  ! %s\n", f.alert[i]);
  lock();
  sFc = f;
  xSemaphoreGive(sMutex);
  return true;
}
