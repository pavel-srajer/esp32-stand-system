#include "ota.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

OtaStatus gOta;
volatile bool gOtaRequest = false;

// "v1.2.3" -> 1002003; neznámý formát -> -1
static long parseVer(const char* v) {
  if (*v == 'v' || *v == 'V') v++;
  int a = 0, b = 0, c = 0;
  if (sscanf(v, "%d.%d.%d", &a, &b, &c) < 2) return -1;
  return a * 1000000L + b * 1000L + c;
}

void otaCheck(bool install) {
  gOta.state = OTA_CHECKING;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("esp32-stand-system/" FW_VERSION);
  if (!http.begin(client, "https://api.github.com/repos/" GITHUB_REPO "/releases/latest")) {
    gOta.state = OTA_FAILED;
    strlcpy(gOta.msg, "HTTP begin", sizeof gOta.msg);
    return;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    http.end();
    gOta.state = OTA_FAILED;
    snprintf(gOta.msg, sizeof gOta.msg, "GitHub HTTP %d", code);
    Serial.printf("[OTA] %s\n", gOta.msg);
    return;
  }
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject fa = filter["assets"].add<JsonObject>();
  fa["name"] = true;
  fa["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    gOta.state = OTA_FAILED;
    snprintf(gOta.msg, sizeof gOta.msg, "JSON %s", err.c_str());
    return;
  }

  const char* tag = doc["tag_name"] | "";
  strlcpy(gOta.latest, tag, sizeof gOta.latest);
  String url;
  for (JsonObject a : doc["assets"].as<JsonArray>())
    if (!strcmp(a["name"] | "", "firmware.bin")) url = (const char*)(a["browser_download_url"] | "");

  long cur = parseVer(FW_VERSION), latest = parseVer(tag);
  Serial.printf("[OTA] běží %s, na GitHubu %s\n", FW_VERSION, tag);
  if (latest < 0 || !url.length()) {
    gOta.state = OTA_FAILED;
    strlcpy(gOta.msg, latest < 0 ? "žádný release" : "release bez firmware.bin", sizeof gOta.msg);
    return;
  }
  if (cur >= 0 && latest <= cur) {
    gOta.state = OTA_UPTODATE;
    return;
  }
  if (!install) {
    gOta.state = OTA_AVAILABLE;
    return;
  }

  Serial.printf("[OTA] stahuji %s\n", url.c_str());
  gOta.state = OTA_DOWNLOADING;
  gOta.progress = 0;
  WiFiClientSecure dl;
  dl.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub přesměrovává na CDN
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) gOta.progress = (int)(100LL * cur / total);
  });
  t_httpUpdate_return r = httpUpdate.update(dl, url);
  // při úspěchu se zařízení restartuje a sem se nedostane
  gOta.state = OTA_FAILED;
  snprintf(gOta.msg, sizeof gOta.msg, "%s", r == HTTP_UPDATE_NO_UPDATES ? "žádná aktualizace"
                                                : httpUpdate.getLastErrorString().c_str());
  Serial.printf("[OTA] chyba: %s\n", gOta.msg);
}
