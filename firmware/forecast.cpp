#include "forecast.h"
#include <time.h>
#include "settings.h"
#include "i18n.h"
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
#define W(cs, en, de, it) return tr(cs, en, de, it)
  switch (c) {
    case 0: W("jasno", "clear", "klar", "sereno");
    case 1: W("skoro jasno", "mostly clear", "überwiegend klar", "poco nuvoloso");
    case 2: W("polojasno", "partly cloudy", "teilweise bewölkt", "parz. nuvoloso");
    case 3: W("zataženo", "overcast", "bedeckt", "coperto");
    case 45: case 48: W("mlha", "fog", "Nebel", "nebbia");
    case 51: case 53: case 55: W("mrholení", "drizzle", "Nieselregen", "pioviggine");
    case 56: case 57: W("mrznoucí mrholení", "freezing drizzle", "gefr. Nieselregen", "pioviggine gelata");
    case 61: W("slabý déšť", "light rain", "leichter Regen", "pioggia debole");
    case 63: W("déšť", "rain", "Regen", "pioggia");
    case 65: W("silný déšť", "heavy rain", "starker Regen", "pioggia forte");
    case 66: case 67: W("mrznoucí déšť", "freezing rain", "gefrierender Regen", "pioggia gelata");
    case 71: W("slabé sněžení", "light snow", "leichter Schneefall", "neve debole");
    case 73: W("sněžení", "snow", "Schneefall", "neve");
    case 75: W("silné sněžení", "heavy snow", "starker Schneefall", "neve forte");
    case 77: W("sněhová zrna", "snow grains", "Schneegriesel", "granuli di neve");
    case 80: case 81: W("přeháňky", "showers", "Schauer", "rovesci");
    case 82: W("silné přeháňky", "heavy showers", "starke Schauer", "rovesci forti");
    case 85: case 86: W("sněhové přeháňky", "snow showers", "Schneeschauer", "rovesci di neve");
    case 95: W("bouřka", "thunderstorm", "Gewitter", "temporale");
    case 96: case 99: W("bouřka s kroupami", "thunderstorm, hail", "Gewitter mit Hagel", "temporale con grandine");
    default: return "";
  }
#undef W
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
  const char* what = cold ? tr("sněžit", "snow", "schneien", "nevicare") : tr("pršet", "rain", "regnen", "piovere");

  // 1) srážky v nejbližších 2 h (15min krok)
  if (f.nm15 > 0) {
    if (f.m15[0] >= WET) {
      int stop = -1;
      for (int i = 1; i < f.nm15; i++)
        if (f.m15[i] < WET) { stop = i; break; }
      const char* now = cold ? tr("Sněží", "Snowing", "Es schneit", "Nevica") : tr("Prší", "Raining", "Es regnet", "Piove");
      if (stop > 0)
        snprintf(f.nowcast, sizeof f.nowcast,
                 tr("%s, přestane asi za %d min", "%s, stops in ~%d min", "%s, endet in ca. %d min", "%s, smette tra ~%d min"),
                 now, stop * 15);
      else
        snprintf(f.nowcast, sizeof f.nowcast,
                 tr("%s, další 2 h nepřestane", "%s for the next 2 h", "%s, die nächsten 2 h", "%s, per le prossime 2 h"), now);
    } else {
      for (int i = 1; i < f.nm15; i++)
        if (f.m15[i] >= WET) {
          if (gCfg.lang == LANG_CS)
            snprintf(f.nowcast, sizeof f.nowcast, "Začne %s asi za %d min", what, i * 15);
          else if (gCfg.lang == LANG_DE)
            snprintf(f.nowcast, sizeof f.nowcast, "In ca. %d min beginnt es zu %s", i * 15, what);
          else if (gCfg.lang == LANG_IT)
            snprintf(f.nowcast, sizeof f.nowcast, "Tra ~%d min inizia a %s", i * 15, what);
          else if (gCfg.lang == LANG_PT)
            snprintf(f.nowcast, sizeof f.nowcast, "Começa a %s em ~%d min", cold ? "nevar" : "chover", i * 15);
          else if (gCfg.lang == LANG_ES)
            snprintf(f.nowcast, sizeof f.nowcast, "Empieza a %s en ~%d min", cold ? "nevar" : "llover", i * 15);
          else if (gCfg.lang == LANG_VI)
            snprintf(f.nowcast, sizeof f.nowcast, "Khoảng %d phút nữa bắt đầu %s", i * 15, cold ? "có tuyết" : "mưa");
          else
            snprintf(f.nowcast, sizeof f.nowcast, "%s starts in ~%d min", cold ? "Snow" : "Rain", i * 15);
          break;
        }
    }
  }
  if (f.nowcast[0]) addAlert(f, AL_WARN, "%s", f.nowcast);

  int h12 = min(f.nh, 12), h24 = min(f.nh, 24);
  // 2) bouřky / mrznoucí srážky / sníh v příštích 12 h
  for (int i = 0; i < h12; i++)
    if (f.hCode[i] >= 95) {
      addAlert(f, AL_DANGER, tr("Bouřky kolem %d:00", "Thunderstorms around %d:00", "Gewitter gegen %d:00", "Temporali verso le %d:00"), f.hHour[i]);
      break;
    }
  for (int i = 0; i < h12; i++)
    if (isFreezing(f.hCode[i]) || (f.hPrec[i] >= 0.1f && f.hT[i] <= 0.5f && f.hT[i] >= -3)) {
      addAlert(f, AL_DANGER, tr("Náledí – mrznoucí srážky kolem %d:00", "Icy roads – freezing rain ~%d:00", "Glatteis – gefr. Regen gegen %d:00", "Ghiaccio – pioggia gelata ~%d:00"), f.hHour[i]);
      break;
    }
  for (int i = 0; i < h12; i++)
    if (isSnow(f.hCode[i])) {
      addAlert(f, AL_WARN, tr("Sněžení kolem %d:00", "Snow around %d:00", "Schneefall gegen %d:00", "Neve verso le %d:00"), f.hHour[i]);
      break;
    }
  // 3) déšť později (když ještě nehlásí nowcast)
  if (!f.nowcast[0]) {
    for (int i = 1; i < h12; i++)
      if (f.hPrec[i] >= 0.3f || f.hProb[i] >= 60) {
        addAlert(f, AL_INFO, tr("Srážky kolem %d:00 (%d %%)", "Rain around %d:00 (%d %%)", "Niederschlag gegen %d:00 (%d %%)", "Pioggia verso le %d:00 (%d %%)"), f.hHour[i], f.hProb[i]);
        break;
      }
  }
  // 4) vítr
  float gMax = 0;
  int gAt = 0;
  for (int i = 0; i < h12; i++)
    if (f.hGust[i] > gMax) { gMax = f.hGust[i]; gAt = f.hHour[i]; }
  if (gMax >= 75) addAlert(f, AL_DANGER, tr("Velmi silný vítr, nárazy %.0f km/h", "Very strong wind, gusts %.0f km/h", "Sturm, Böen %.0f km/h", "Vento molto forte, raffiche %.0f km/h"), gMax);
  else if (gMax >= 50) addAlert(f, AL_WARN, tr("Silný vítr, nárazy %.0f km/h (%d:00)", "Strong wind, gusts %.0f km/h (%d:00)", "Starker Wind, Böen %.0f km/h (%d:00)", "Vento forte, raffiche %.0f km/h (%d:00)"), gMax, gAt);
  // 5) mráz / horko v příštích 24 h
  float tMin = 99, tMax = -99;
  int minAt = 0, maxAt = 0;
  for (int i = 0; i < h24; i++) {
    if (f.hT[i] < tMin) { tMin = f.hT[i]; minAt = f.hHour[i]; }
    if (f.hT[i] > tMax) { tMax = f.hT[i]; maxAt = f.hHour[i]; }
  }
  if (tMin <= -10) addAlert(f, AL_DANGER, tr("Silný mráz až %.0f °C (%d:00)", "Severe frost %.0f °C (%d:00)", "Strenger Frost %.0f °C (%d:00)", "Gelo intenso %.0f °C (%d:00)"), tMin, minAt);
  else if (tMin <= 0) addAlert(f, AL_INFO, tr("Mráz až %.0f °C (%d:00)", "Frost down to %.0f °C (%d:00)", "Frost bis %.0f °C (%d:00)", "Gelo fino a %.0f °C (%d:00)"), tMin, minAt);
  if (tMax >= 33) addAlert(f, AL_DANGER, tr("Velké horko až %.0f °C", "Extreme heat up to %.0f °C", "Große Hitze bis %.0f °C", "Caldo intenso fino a %.0f °C"), tMax);
  else if (tMax >= 30) addAlert(f, AL_WARN, tr("Horko až %.0f °C (%d:00)", "Hot, up to %.0f °C (%d:00)", "Hitze bis %.0f °C (%d:00)", "Caldo fino a %.0f °C (%d:00)"), tMax, maxAt);
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
           "&timezone=Europe%%2FPrague&wind_speed_unit=kmh&timeformat=unixtime",
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
    appendConnReason(sFc.err, sizeof sFc.err, client, code);
    xSemaphoreGive(sMutex);
    Serial.printf("[Forecast] HTTP %d\n", code);
    return false;
  }
  BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
  // Jen potřebné údaje (bez jednotek a metadat) a časy jako čísla místo textů:
  // dokument zabere v RAM zhruba polovinu (běží ještě během šifrovaného spojení).
  JsonDocument filter;
  for (const char* k : {"temperature_2m", "weather_code", "wind_speed_10m", "wind_gusts_10m", "is_day"}) filter["current"][k] = true;
  for (const char* k : {"time", "temperature_2m", "precipitation", "precipitation_probability", "weather_code", "wind_gusts_10m"})
    filter["hourly"][k] = true;
  filter["minutely_15"]["precipitation"] = true;
  for (const char* k : {"time", "weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_sum"}) filter["daily"][k] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
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
    time_t tt = (time_t)(ht[i] | 0L);  // unixtime (UTC) -> místní hodina
    struct tm lt;
    localtime_r(&tt, &lt);
    f.hHour[i] = lt.tm_hour;
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
