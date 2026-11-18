#include "route.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const int CACHE = 6;

struct Entry {
  char cs[10];
  bool found;      // false = trasa neznámá (nezkoušet znovu)
  uint32_t used;   // pro LRU
  RouteInfo r;
};
static Entry sCache[CACHE];
static char sPending[10] = "";
static float sPendLat = 0, sPendLon = 0;
static uint32_t sRetryAt = 0;
static char sStatus[120] = "–";
static SemaphoreHandle_t sMutex = nullptr;

static void lock() {
  if (!sMutex) sMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(sMutex, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(sMutex); }

const char* routeStatus() { return sStatus; }

bool routeGet(const char* cs, float lat, float lon, RouteInfo& out) {
  if (!cs || !cs[0]) return false;
  bool have = false, known = false;
  lock();
  for (auto& e : sCache)
    if (e.cs[0] && !strcmp(e.cs, cs)) {
      known = true;
      e.used = millis();
      if (e.found) {
        out = e.r;
        have = true;
      }
    }
  if (!known) {
    strlcpy(sPending, cs, sizeof sPending);
    sPendLat = lat;
    sPendLon = lon;
  }
  unlock();
  return have;
}

static void httpSetup(HTTPClient& http) {
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("esp32-stand-system/1.0");
}

// 1 = nalezeno, 0 = neznámá trasa, -1 = chyba sítě
static int fromAdsbLol(const char* cs, float lat, float lon, RouteInfo& r) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  httpSetup(http);
  if (!http.begin(client, "https://api.adsb.lol/api/0/routeset")) return -1;
  http.addHeader("Content-Type", "application/json");
  char body[128];
  snprintf(body, sizeof body, "{\"planes\":[{\"callsign\":\"%s\",\"lat\":%.4f,\"lng\":%.4f}]}", cs, lat, lon);
  int code = http.POST(body);
  if (code != 200) {
    http.end();
    snprintf(sStatus, sizeof sStatus, "%s: adsb.lol HTTP %d", cs, code);
    return code < 0 ? -1 : 0;
  }
  JsonDocument filter;
  for (const char* k : {"iata", "icao", "location", "name"}) filter[0]["_airports"][0][k] = true;
  filter[0]["plausible"] = true;
  JsonDocument doc;
  BodyStream s(http.getStreamPtr(), BodyStream::isChunked(http));
  DeserializationError err = deserializeJson(doc, s, DeserializationOption::Filter(filter));
  http.end();
  if (err) return -1;
  JsonArray aps = doc[0]["_airports"];
  if (aps.size() < 2) return 0;
  auto fill = [](Airport& a, JsonObject o) {
    strlcpy(a.iata, o["iata"] | "", sizeof a.iata);
    strlcpy(a.icao, o["icao"] | "", sizeof a.icao);
    strlcpy(a.city, o["location"] | "", sizeof a.city);
    strlcpy(a.name, o["name"] | "", sizeof a.name);
  };
  fill(r.from, aps[0]);
  fill(r.to, aps[aps.size() - 1]);
  JsonVariant pl = doc[0]["plausible"];
  r.plausible = pl.isNull() || pl.as<bool>() || pl.as<int>() != 0;
  return (r.from.icao[0] && r.to.icao[0] && strcmp(r.from.icao, r.to.icao)) ? 1 : 0;
}

static int fromAdsbdb(const char* cs, RouteInfo& r) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  httpSetup(http);
  char url[80];
  snprintf(url, sizeof url, "https://api.adsbdb.com/v0/callsign/%s", cs);
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code == 404) {  // neznámý volací znak
    http.end();
    return 0;
  }
  if (code != 200) {
    http.end();
    snprintf(sStatus, sizeof sStatus, "%s: adsbdb HTTP %d", cs, code);
    return -1;
  }
  JsonDocument filter;
  for (const char* side : {"origin", "destination"})
    for (const char* k : {"iata_code", "icao_code", "municipality", "name"}) filter["response"]["flightroute"][side][k] = true;
  JsonDocument doc;
  BodyStream s(http.getStreamPtr(), BodyStream::isChunked(http));
  DeserializationError err = deserializeJson(doc, s, DeserializationOption::Filter(filter));
  http.end();
  if (err) return -1;
  JsonObject fr = doc["response"]["flightroute"];
  auto fill = [](Airport& a, JsonObject o) {
    strlcpy(a.iata, o["iata_code"] | "", sizeof a.iata);
    strlcpy(a.icao, o["icao_code"] | "", sizeof a.icao);
    strlcpy(a.city, o["municipality"] | "", sizeof a.city);
    strlcpy(a.name, o["name"] | "", sizeof a.name);
  };
  fill(r.from, fr["origin"]);
  fill(r.to, fr["destination"]);
  r.plausible = true;
  return (r.from.icao[0] && r.to.icao[0] && strcmp(r.from.icao, r.to.icao)) ? 1 : 0;
}

void routeService() {
  if ((int32_t)(sRetryAt - millis()) > 0) return;
  char cs[10];
  float lat, lon;
  lock();
  strlcpy(cs, sPending, sizeof cs);
  lat = sPendLat;
  lon = sPendLon;
  sPending[0] = 0;
  unlock();
  if (!cs[0]) return;

  RouteInfo r;
  int res = fromAdsbLol(cs, lat, lon, r);
  if (res <= 0) {
    RouteInfo r2;
    int res2 = fromAdsbdb(cs, r2);
    if (res2 > 0) {
      r = r2;
      res = 1;
    } else if (res2 == 0 && res == 0) {
      res = 0;
    } else {
      res = -1;
    }
  }
  if (res < 0) {  // síťová chyba: zkusit znovu za 30 s
    sRetryAt = millis() + 30000;
    Serial.printf("[Trasa] %s\n", sStatus);
    return;
  }
  if (res > 0)
    snprintf(sStatus, sizeof sStatus, "%s: %s → %s (%s → %s)%s", cs, r.from.iata, r.to.iata, r.from.city, r.to.city,
             r.plausible ? "" : " (?)");
  else snprintf(sStatus, sizeof sStatus, "%s: –", cs);
  Serial.printf("[Trasa] %s\n", sStatus);

  lock();
  int slot = 0;  // nejdéle nepoužité místo
  for (int i = 1; i < CACHE; i++)
    if (sCache[i].used < sCache[slot].used) slot = i;
  strlcpy(sCache[slot].cs, cs, sizeof sCache[slot].cs);
  sCache[slot].found = res > 0;
  sCache[slot].r = r;
  sCache[slot].used = millis();
  unlock();
}
