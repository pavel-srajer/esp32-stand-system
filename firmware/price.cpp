#include "price.h"
#include "settings.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

static const char* URL_SPOTEL = "https://spotovaelektrina.cz/api/v1/price/get-prices-json-qh";
static const char* URL_SPOTOVKY = "https://api.spotovky.cz/v2/prices/";  // + today / tomorrow

const char* priceSrcName(int src) {
  return src == 0 ? "spotovaelektrina.cz" : src == 1 ? "spotovky.cz" : "";
}

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

static bool beginGet(HTTPClient& http, WiFiClientSecure& client, const String& url) {
  client.setInsecure();
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
  return http.begin(client, url);
}

// spotovaelektrina.cz: jedna odpověď s poli hoursToday a hoursTomorrow,
// položky {hour, minute, priceCZK [Kč/MWh]}.
// Pole se čtou po jednom prvku přímo ze sítě (192 položek najednou by zabralo
// ~10 kB RAM). Pořadí klíčů v odpovědi nehraje roli.
static void fetchSpotEl(float* today, float* tomorrow, int& nT, int& nZ, char* err, size_t errLen) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!beginGet(http, client, URL_SPOTEL)) {
    strlcpy(err, "HTTP begin", errLen);
    return;
  }
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
    snprintf(err, errLen, "HTTP %d", code);
  }
  http.end();
}

// spotovky.cz (API v2): zvlášť /today a /tomorrow (zítřek je do zveřejnění 404).
// {"rate":24.47,"slots":[{"start":"2026-11-24T00:15:00+01:00","czk":4.9,"eur":200.42},...]}
// czk je zaokrouhlené na 2 desetinná místa, proto počítáme eur × kurz (Kč/MWh).
// Vrací počet načtených čtvrthodin, -404 když den ještě není, jinak -kód chyby.
static int fetchSpotovkyDay(const char* day, float* dst, char* err, size_t errLen) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!beginGet(http, client, String(URL_SPOTOVKY) + day)) {
    strlcpy(err, "HTTP begin", errLen);
    return -1;
  }
  int code = http.GET(), n = 0;
  if (code == 200) {
    BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
    body.setTimeout(10000);
    float rate = 0;
    // klíče jdou v pořadí ..., "rate", "slots" – kurz tedy známe dřív než ceny
    if (body.find("\"rate\"") && body.find(":")) rate = body.parseFloat();
    if (body.find("\"slots\"") && body.find("[")) {
      JsonDocument filter;
      filter["start"] = true;
      filter["czk"] = true;
      filter["eur"] = true;
      JsonDocument doc;
      do {
        if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) break;
        const char* st = doc["start"] | "";  // YYYY-MM-DDTHH:MM:SS+hh:mm
        if (strlen(st) < 16) continue;
        int h = atoi(st + 11), m = atoi(st + 14);
        int slot = h * 4 + m / 15;
        if (h < 0 || h > 23 || slot < 0 || slot >= PRICE_SLOTS) continue;
        JsonVariant ev = doc["eur"], cv = doc["czk"];
        if (rate > 0 && !ev.isNull()) dst[slot] = ev.as<float>() * rate;
        else if (!cv.isNull()) dst[slot] = cv.as<float>() * 1000.0f;
        else continue;
        n++;
      } while (body.findUntil(",", "]"));
    }
  } else {
    n = code == 404 ? -404 : -1;
    if (code != 404) snprintf(err, errLen, "HTTP %d", code);
  }
  http.end();
  return n;
}

static void fetchSpotovky(float* today, float* tomorrow, int& nT, int& nZ, char* err, size_t errLen) {
  int t = fetchSpotovkyDay("today", today, err, errLen);
  if (t == -404) strlcpy(err, "HTTP 404", errLen);
  if (t <= 0) return;
  nT = t;
  char e2[48] = "";
  int z = fetchSpotovkyDay("tomorrow", tomorrow, e2, sizeof e2);
  nZ = z > 0 ? z : 0;
}

static void fetchFrom(int src, float* today, float* tomorrow, int& nT, int& nZ, char* err, size_t errLen) {
  clearDay(today);
  clearDay(tomorrow);
  nT = nZ = 0;
  err[0] = 0;
  if (src == 1) fetchSpotovky(today, tomorrow, nT, nZ, err, errLen);
  else fetchSpotEl(today, tomorrow, nT, nZ, err, errLen);
  if (!nT && !err[0]) strlcpy(err, "prázdná odpověď", errLen);
  Serial.printf("[Cena] %s: dnes %d, zítra %d čtvrthodin %s\n", priceSrcName(src), nT, nZ, err);
}

bool priceUpdate() {
  static float today[PRICE_SLOTS], tomorrow[PRICE_SLOTS];
  int nT = 0, nZ = 0;
  int src = gCfg.priceSrc == 1 ? 1 : 0;
  char err[48] = "";
  fetchFrom(src, today, tomorrow, nT, nZ, err, sizeof err);
  if (!nT) {  // zvolený zdroj nejde -> záloha
    char e1[48];
    snprintf(e1, sizeof e1, "%s: %s", priceSrcName(src), err);
    src = 1 - src;
    fetchFrom(src, today, tomorrow, nT, nZ, err, sizeof err);
    if (!nT) strlcpy(err, e1, sizeof err);
  }

  lock();
  if (nT) {
    memcpy(sP.today, today, sizeof today);
    memcpy(sP.tomorrow, tomorrow, sizeof tomorrow);
    sP.todayOk = true;
    sP.tomorrowOk = nZ > 0;
    sP.updatedMs = millis();
    sP.err[0] = 0;
    sP.src = src;
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

void priceWindows(const PriceData& p, int curSlot, PriceWinResult* out) {
  int end = p.tomorrowOk ? 2 * PRICE_SLOTS : PRICE_SLOTS;  // za poslední známou čtvrthodinou
  auto at = [&](int t) -> float {
    if (t < PRICE_SLOTS) return p.todayOk ? priceFinal(p.today[t]) : NAN;
    return priceFinal(p.tomorrow[t - PRICE_SLOTS]);
  };
  for (int i = 0; i < PRICE_WINS; i++) {
    const PriceWin& w = gCfg.priceWin[i];
    out[i] = PriceWinResult();
    if (!w.on || w.dur < 1) continue;
    int span = ((w.to - w.from) % 1440 + 1440) % 1440;
    if (span == 0) span = 1440;  // od == do -> celý den
    float best = 1e9;
    for (int t = max(curSlot, 0); t + w.dur <= end; t++) {
      int sm = (t % PRICE_SLOTS) * 15;
      int rel = ((sm - w.from) % 1440 + 1440) % 1440;
      if (rel + w.dur * 15 > span) continue;  // okno se nevejde do povoleného času
      float sum = 0;
      bool okAll = true;
      for (int k = 0; k < w.dur && okAll; k++) {
        float v = at(t + k);
        if (isnan(v)) okAll = false;
        else sum += v;
      }
      if (!okAll) continue;
      float avg = sum / w.dur;
      if (avg < best - 1e-6f) {
        best = avg;
        out[i].ok = true;
        out[i].start = t;
        out[i].avg = avg;
      }
    }
  }
}
