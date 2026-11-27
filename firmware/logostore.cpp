#include "logostore.h"
#include "http_body.h"
#include <esp_partition.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <rom/crc.h>
#include <stddef.h>
#include <time.h>

// Rozložení: sektor 4 kB = 8 dílků po 512 B. Záznam = hlavička + RLE data a zabírá
// souvislé dílky v jednom sektoru. V sektoru se zapisuje jen na konec (do smazané,
// tj. 0xFF části), takže se nikdy nepřepisuje jiné logo. Smazání = přepsání stavu
// na 0x00 (bity flash jde jen nulovat). Místo se uvolní až smazáním celého sektoru.

static const int SECT = 4096, SLOT = 512, SPS = SECT / SLOT;
static const uint16_t MAGIC = 0x4C47;  // "GL"
static const uint8_t ST_VALID = 0xA5, ST_DEL = 0x00;

struct __attribute__((packed)) Hdr {
  uint16_t magic;
  uint8_t state;
  uint8_t nslots;
  char icao[4];
  uint8_t found;
  uint8_t pad;
  uint16_t len;
  uint32_t gen;
  uint32_t crc;   // CRC32 dat – výpadek proudu při zápisu nezpůsobí poškozené logo
  char etag[68];
};
static_assert(sizeof(Hdr) <= SLOT, "hlavička musí být v jednom dílku");

static const esp_partition_t* sPart = nullptr;
static int sSectors = 0;
static int sCount = -1;
static uint32_t sGen = 1;

static bool storeInit() {
  if (sPart) return true;
  sPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!sPart) return false;
  sSectors = sPart->size / SECT;
  Preferences p;
  p.begin("logo", false);
  if (!p.getBool("fmt", false)) {  // první použití: oddíl smazat (mohl obsahovat cokoli)
    esp_partition_erase_range(sPart, 0, (size_t)sSectors * SECT);
    p.putBool("fmt", true);
    Serial.printf("[Logo] úložiště ve flash připraveno (%d kB)\n", sSectors * 4);
  }
  sGen = p.getUInt("gen", 1);
  p.end();
  return true;
}

static bool readHdr(int slot, Hdr& h) { return esp_partition_read(sPart, (size_t)slot * SLOT, &h, sizeof h) == ESP_OK; }

static bool isErased(const Hdr& h) {
  const uint8_t* b = (const uint8_t*)&h;
  for (size_t i = 0; i < sizeof h; i++)
    if (b[i] != 0xFF) return false;
  return true;
}

static bool isRecord(const Hdr& h, int i) { return h.magic == MAGIC && h.nslots >= 1 && i + h.nslots <= SPS; }

// Projde všechny záznamy; fn(dílek, hlavička) vrátí true = konec.
template <typename F>
static void forEach(F fn) {
  for (int s = 0; s < sSectors; s++) {
    int i = 0;
    while (i < SPS) {
      Hdr h;
      if (!readHdr(s * SPS + i, h)) return;
      if (isRecord(h, i)) {
        if (fn(s * SPS + i, h)) return;
        i += h.nslots;
      } else if (isErased(h)) {
        break;  // zbytek sektoru je volný
      } else {
        i++;  // poškozený dílek přeskočit
      }
    }
  }
}

static int freeTail(int s) {  // počet volných dílků na konci sektoru
  int i = 0;
  while (i < SPS) {
    Hdr h;
    if (!readHdr(s * SPS + i, h)) return 0;
    if (isRecord(h, i)) i += h.nslots;
    else if (isErased(h)) return SPS - i;
    else i++;
  }
  return 0;
}

bool logoStoreFind(const char* icao, LogoRec& rec, uint8_t* out, int maxLen) {
  if (!storeInit()) return false;
  bool ok = false;
  forEach([&](int slot, const Hdr& h) {
    if (h.state != ST_VALID || strncmp(h.icao, icao, 4) != 0) return false;
    if (h.len > maxLen) return false;
    if (h.len && esp_partition_read(sPart, (size_t)slot * SLOT + sizeof(Hdr), out, h.len) != ESP_OK) return true;
    if (crc32_le(0, out, h.len) != h.crc) return false;  // poškozený záznam – hledat dál
    rec.found = h.found;
    rec.gen = h.gen;
    rec.len = h.len;
    memcpy(rec.etag, h.etag, sizeof rec.etag);
    rec.etag[sizeof rec.etag - 1] = 0;
    ok = true;
    return true;
  });
  return ok;
}

static void markDeleted(const char* icao) {
  forEach([&](int slot, const Hdr& h) {
    if (h.state == ST_VALID && strncmp(h.icao, icao, 4) == 0) {
      uint8_t z = ST_DEL;
      esp_partition_write(sPart, (size_t)slot * SLOT + offsetof(Hdr, state), &z, 1);
    }
    return false;
  });
}

bool logoStorePut(const char* icao, const LogoRec& rec, const uint8_t* rle, uint8_t* buf) {
  if (!storeInit()) return false;
  int total = sizeof(Hdr) + rec.len;
  int ns = (total + SLOT - 1) / SLOT;
  if (ns > SPS) return false;
  markDeleted(icao);
  int sect = -1, at = 0;
  for (int s = 0; s < sSectors && sect < 0; s++) {
    int ft = freeTail(s);
    if (ft >= ns) {
      sect = s;
      at = SPS - ft;
    }
  }
  if (sect < 0) {  // místo došlo: smazat další sektor v pořadí (nejdéle zapsaná loga)
    Preferences p;
    p.begin("logo", false);
    int rr = p.getInt("rr", 0) % sSectors;
    p.putInt("rr", (rr + 1) % sSectors);
    p.end();
    if (esp_partition_erase_range(sPart, (size_t)rr * SECT, SECT) != ESP_OK) return false;
    sect = rr;
    at = 0;
    Serial.printf("[Logo] flash plná – smazán sektor %d\n", rr);
  }
  Hdr h;
  memset(&h, 0, sizeof h);
  h.magic = MAGIC;
  h.state = ST_VALID;
  h.nslots = ns;
  strncpy(h.icao, icao, sizeof h.icao);
  h.found = rec.found;
  h.len = rec.len;
  h.gen = rec.gen;
  h.crc = crc32_le(0, rle, rec.len);
  strlcpy(h.etag, rec.etag, sizeof h.etag);
  memcpy(buf, &h, sizeof h);
  if (rec.len) memcpy(buf + sizeof h, rle, rec.len);
  bool ok = esp_partition_write(sPart, (size_t)(sect * SPS + at) * SLOT, buf, total) == ESP_OK;
  sCount = -1;
  return ok;
}

uint32_t logoStoreGen() {
  storeInit();
  return sGen;
}

void logoStoreBumpGen() {
  if (!storeInit()) return;
  sGen++;
  Preferences p;
  p.begin("logo", false);
  p.putUInt("gen", sGen);
  p.end();
}

int logoStoreCount() {
  if (!storeInit()) return 0;
  if (sCount >= 0) return sCount;
  int n = 0;
  forEach([&](int, const Hdr& h) {
    if (h.state == ST_VALID && h.found) n++;
    return false;
  });
  sCount = n;
  return n;
}

bool logoStoreWeeklyCheck() {
  if (!storeInit()) return true;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  Preferences p;
  p.begin("logo", true);
  uint32_t last = p.getUInt("chk", 0);
  String sha = p.getString("sha", "");
  p.end();
  if (last && (uint32_t)now - last < 7 * 86400UL) return false;

  // Poslední změna složky s logy ve sbírce na GitHubu (jeden malý dotaz).
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient http;
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("esp32-stand-system");
  String newSha;
  if (http.begin(c, "https://api.github.com/repos/Jxck-S/airline-logos/commits?path=flightaware_logos&per_page=1")) {
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code == 200) {
      BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http));
      JsonDocument filter;
      filter[0]["sha"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) newSha = (const char*)(doc[0]["sha"] | "");
    }
    http.end();
  }
  p.begin("logo", false);
  // při chybě to zkusit zase za den (místo za týden)
  p.putUInt("chk", newSha.length() ? (uint32_t)now : (uint32_t)now - 6 * 86400UL);
  if (newSha.length()) p.putString("sha", newSha);
  p.end();
  if (newSha.length() && sha.length() && sha != newSha) {
    logoStoreBumpGen();  // loga se změnila -> každé se při dalším zobrazení ověří
    Serial.printf("[Logo] sbírka log se změnila – loga se postupně ověří (generace %u)\n", (unsigned)sGen);
  } else {
    Serial.printf("[Logo] týdenní kontrola: %s\n", newSha.length() ? "beze změny" : "nepovedla se");
  }
  return true;
}
