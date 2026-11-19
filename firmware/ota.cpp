#include "ota.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "http_body.h"
#include "settings.h"

OtaStatus gOta;
volatile bool gOtaRequest = false;
bool gOtaBootMode = false;

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
  BodyStream::prepare(http);
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
  BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject fa = filter["assets"].add<JsonObject>();
  fa["name"] = true;
  fa["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
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
  if (!gOtaBootMode) {
    // Za běhu je RAM rozdrobená. Adresu si uložíme a stáhneme hned po restartu,
    // kdy je paměť volná a nemusí se znovu volat GitHub API.
    Serial.println("[OTA] nová verze – restart a instalace po startu");
    settingsSetOtaUrl(url);
    settingsSetOtaFlag(true);
    delay(200);
    ESP.restart();
  }
  otaInstall(url);
}

// Zjistí cíl přesměrování (GitHub -> release-assets.githubusercontent.com) a spojení
// ÚPLNĚ zavře. HTTPClient by při přesměrování na jiný server nechal první spojení
// otevřené a otevřel druhé – dvě TLS spojení (~90 kB) se do RAM ESP32-C3 nevejdou.
static String resolveRedirect(const String& url) {
  // GET bez následování přesměrování; čteme jen hlavičky (tělo odpovědi 302 je prázdné).
  // Pozn.: HEAD nejde – GitHub na něj vrací jinou adresu, kterou CDN pro stažení odmítne.
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.setReuse(false);
  h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  h.setConnectTimeout(8000);
  h.setTimeout(10000);
  h.setUserAgent("esp32-stand-system/" FW_VERSION);
  static const char* hdrs[] = {"Location"};
  h.collectHeaders(hdrs, 1);
  String target = url;
  if (h.begin(c, url)) {
    int code = h.GET();
    String loc = h.header("Location");
    if (code / 100 == 3 && loc.length()) target = loc;
    Serial.printf("[OTA] GitHub %d%s\n", code, target != url ? " -> přesměrování" : "");
    h.end();  // setReuse(false): spojení se opravdu zavře a uvolní paměť
  }
  return target;
}

void otaInstall(const String& url) {
  for (int attempt = 1; attempt <= 3; attempt++) {
    gOta.state = OTA_DOWNLOADING;
    gOta.progress = 0;
    String target = resolveRedirect(url);
    Serial.printf("[OTA] pokus %d, stahuji (%u znaků adresy), volná RAM %u kB, blok %u kB\n", attempt,
                  target.length(), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
    WiFiClientSecure dl;
    dl.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);
    httpUpdate.onProgress([](int cur, int total) {
      if (total > 0) gOta.progress = (int)(100LL * cur / total);
    });
    t_httpUpdate_return r = httpUpdate.update(dl, target);
    // při úspěchu se zařízení restartuje a sem se nedostane
    char tls[40] = "";
    dl.lastError(tls, sizeof tls);
    snprintf(gOta.msg, sizeof gOta.msg, "%s%s%s (blok %u kB)",
             r == HTTP_UPDATE_NO_UPDATES ? "žádná aktualizace" : httpUpdate.getLastErrorString().c_str(),
             tls[0] ? ", TLS: " : "", tls, (unsigned)(ESP.getMaxAllocHeap() / 1024));
    Serial.printf("[OTA] pokus %d selhal: %s\n", attempt, gOta.msg);
    delay(3000);
  }
  gOta.state = OTA_FAILED;
}
