#include "price.h"
#include "settings.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

static const char* PRICE_URL = "https://spotovaelektrina.cz/api/v1/price/get-prices-json-qh";

static PriceData sP;
static int sYday = -1;  // den v roce, ke kterému patří sP.today
static SemaphoreHandle_t sMutex = nullptr;

static void lock() {
  if (!sMutex) sMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(sMutex, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(sMutex); }

void priceGet(PriceData& out) {
  lock();
  out = sP;
  unlock();
}

float priceFinal(float czkMwh) {
  if (isnan(czkMwh)) return NAN;
  return (czkMwh / 1000.0f + gCfg.priceFee) * (1.0f + gCfg.priceVat / 100.0f);
}

static void clearDay(float* a) {
  for (int i = 0; i < PRICE_SLOTS; i++) a[i] = NAN;
}

static bool timeOk() { return time(nullptr) > 1700000000; }

// Pole se čtou po jednom prvku přímo ze sítě (192 položek najednou by zabralo
// ~10 kB RAM). Pořadí klíčů v odpovědi nehraje roli.
bool priceUpdate() {
  static float today[PRICE_SLOTS], tomorrow[PRICE_SLOTS];
  clearDay(today);
  clearDay(tomorrow);
  int nT = 0, nZ = 0;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
  char err[48] = "";
  if (http.begin(client, PRICE_URL)) {
    int code = http.GET();
    if (code == 200) {
      BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
      body.setTimeout(10000);
      JsonDocument filter;
      filter["hour"] = true;
      filter["minute"] = true;
      filter["priceCZK"] = true;
      JsonDocument doc;
      while (body.find("\"hours")) {  // "hoursToday" / "hoursTomorrow"
        char key[12];
        int k = 0, ch;
        while ((ch = body.read()) >= 0 && ch != '"' && k < 11) key[k++] = ch;
        key[k] = 0;
        float* dst = !strcmp(key, "Today") ? today : !strcmp(key, "Tomorrow") ? tomorrow : nullptr;
        if (!dst || !body.find("[")) continue;
        int& cnt = dst == today ? nT : nZ;
        do {
          if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) break;  // prázdné pole
          JsonVariant hv = doc["hour"], pv = doc["priceCZK"];
          if (hv.isNull() || pv.isNull()) continue;
          int h = hv.as<int>(), m = doc["minute"] | 0;
          int slot = h * 4 + m / 15;
          if (h < 0 || h > 23 || slot < 0 || slot >= PRICE_SLOTS) continue;
          dst[slot] = pv.as<float>();
          cnt++;
        } while (body.findUntil(",", "]"));
      }
    } else {
      snprintf(err, sizeof err, "HTTP %d", code);
    }
    http.end();
  }
  if (!nT && !err[0]) strlcpy(err, "prázdná odpověď", sizeof err);
  Serial.printf("[Cena] dnes %d, zítra %d čtvrthodin %s\n", nT, nZ, err);

  lock();
  if (nT) {
    memcpy(sP.today, today, sizeof today);
    memcpy(sP.tomorrow, tomorrow, sizeof tomorrow);
    sP.todayOk = true;
    sP.tomorrowOk = nZ > 0;
    sP.updatedMs = millis();
    sP.err[0] = 0;
    if (timeOk()) {
      time_t now = time(nullptr);
      struct tm lt;
      localtime_r(&now, &lt);
      sYday = lt.tm_yday;
    }
  } else {
    strlcpy(sP.err, err, sizeof sP.err);
  }
  unlock();
  return nT > 0;
}

// O půlnoci se zítřejší ceny (pokud jsou) hned stanou dnešními.
void priceTick() {
  if (!timeOk()) return;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  lock();
  if (sYday >= 0 && lt.tm_yday != sYday) {
    if (sP.tomorrowOk) {
      memcpy(sP.today, sP.tomorrow, sizeof sP.today);
      clearDay(sP.tomorrow);
      sP.tomorrowOk = false;
    } else {
      sP.todayOk = false;
    }
    sYday = lt.tm_yday;
  }
  unlock();
}
