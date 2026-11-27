#include "logo.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "png_net.h"
#include "radar.h"
#include "logostore.h"
#include <LovyanGFX.hpp>

static const char* LOGO_URL = "https://raw.githubusercontent.com/Jxck-S/airline-logos/main/flightaware_logos/%s.png";
// Loga se drží komprimovaná (RLE: páry počet, barva) za sebou v pevném bloku.
// Logo 56×56 má nekomprimované 3 136 B, komprimované typicky 0,2–1,5 kB (průměr
// ~750 B), takže se do 6 kB vejde zhruba 8 log (dřív 3 nekomprimovaná do 9,4 kB).
// Pevný blok navíc nedrobí haldu (jinak by chyběl souvislý blok pro šifrování).
static const int CACHE = 16;                       // max. počet log (i "logo neexistuje")
static const int POOL = 2 * LOGO_SIZE * LOGO_SIZE;  // 6 272 B ≈ 8 log

struct Entry {
  char icao[4];    // "" = volné místo
  bool found;      // false = logo neexistuje (404), nezkoušet znovu
  uint32_t used;   // pro LRU
  uint16_t off, len;  // RLE v sPool
};
static Entry sCache[CACHE];
static uint8_t sPool[POOL];
static char sPending[4] = "";
static uint32_t sRetryAt = 0;  // po chybě sítě chvíli nezkoušet
static char sStatus[96] = "zatím žádné";
const char* logoStatus() { return sStatus; }
static SemaphoreHandle_t sMutex = nullptr;

static void lock() {
  if (!sMutex) sMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(sMutex, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(sMutex); }

bool logoIcaoFromCallsign(const char* cs, char icao[4]) {
  // letecké volací znaky: 3 písmena + číslice (CSA4LK, RYR12AB); registrace (OKNEB) ne
  for (int i = 0; i < 3; i++)
    if (cs[i] < 'A' || cs[i] > 'Z') return false;
  if (cs[3] < '0' || cs[3] > '9') return false;
  memcpy(icao, cs, 3);
  icao[3] = 0;
  return true;
}

bool logoGet(const char* icao, uint8_t* out) {
  bool have = false, known = false;
  lock();
  for (auto& e : sCache)
    if (e.icao[0] && !strcmp(e.icao, icao)) {
      known = true;
      e.used = millis();
      if (e.found && e.len) {  // rozbalit RLE
        int pos = 0;
        for (int i = 0; i + 1 < e.len && pos < LOGO_SIZE * LOGO_SIZE; i += 2) {
          int n = min((int)sPool[e.off + i], LOGO_SIZE * LOGO_SIZE - pos);
          memset(out + pos, sPool[e.off + i + 1], n);
          pos += n;
        }
        have = pos == LOGO_SIZE * LOGO_SIZE;
      }
    }
  if (!known) strlcpy(sPending, icao, sizeof sPending);
  unlock();
  return have;
}

struct Ctx {
  PNG* png;
  uint16_t* line;
  uint8_t* out;
  int W, H;
  int nextRow;
};

static int drawCb(PNGDRAW* d) {
  Ctx* c = (Ctx*)d->pUser;
  bool need = false;
  int y0 = c->nextRow;
  while (y0 < LOGO_SIZE && y0 * c->H / LOGO_SIZE == d->y) {
    need = true;
    y0++;
  }
  if (!need) return 1;
  c->png->getLineAsRGB565(d, c->line, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
  for (int oy = c->nextRow; oy < y0; oy++)
    for (int ox = 0; ox < LOGO_SIZE; ox++) {
      // průměr 2×2 zdrojových pixelů v řádku (zmenšení 90 -> 56 vypadá lépe)
      int sx = ox * c->W / LOGO_SIZE, sx2 = min(sx + 1, c->W - 1);
      uint16_t a = c->line[sx], b = c->line[sx2];
      int r = (((a >> 11) & 31) + ((b >> 11) & 31)) * 4, g = (((a >> 5) & 63) + ((b >> 5) & 63)) * 2,
          bl = ((a & 31) + (b & 31)) * 4;
      c->out[oy * LOGO_SIZE + ox] = lgfx::color332(r, g, bl);
    }
  c->nextRow = y0;
  return 1;
}

static bool logoReady(PNG& png, void* user) {
  Ctx* c = (Ctx*)user;
  c->png = &png;
  c->W = png.getWidth();
  c->H = png.getHeight();
  c->nextRow = 0;
  c->line = (uint16_t*)malloc(c->W * 2);
  return c->line != nullptr;
}

// stáhne a dekóduje; vrací: 1 = OK, 0 = logo neexistuje, -1 = chyba (zkusit později)
// stáhne a dekóduje; vrací 1 = OK, 0 = logo neexistuje (404), 2 = nezměněno (304),
// -1 = chyba (zkusit později)
static int fetch(const char* icao, uint8_t* out, const char* ifNoneMatch, char* etag, size_t etagLen) {
  char url[128];
  snprintf(url, sizeof url, LOGO_URL, icao);
  Ctx c{};
  c.out = out;
  memset(out, 0, LOGO_SIZE * LOGO_SIZE);
  int code = 0;
  bool ok = pngDecodeUrlEx(url, drawCb, logoReady, &c, code, ifNoneMatch, etag, etagLen);
  free(c.line);
  if (ok) return 1;
  if (code == 304) return 2;
  return code == 404 ? 0 : -1;
}

static int rleEncode(const uint8_t* px, uint8_t* out, int maxLen) {
  const int N = LOGO_SIZE * LOGO_SIZE;
  int o = 0;
  for (int i = 0; i < N && o + 2 <= maxLen;) {
    int n = 1;
    while (i + n < N && px[i + n] == px[i] && n < 255) n++;
    out[o++] = n;
    out[o++] = px[i];
    i += n;
  }
  return o;
}

// Vloží logo (RLE) do RAM cache; nejdéle nepoužitá loga uvolní, zbylá setřese.
static void cacheInsert(const char* icao, bool found, const uint8_t* rle, int len) {
  if (len > POOL) len = 0, found = false;
  lock();
  auto usedBytes = [&]() {
    int u = 0;
    for (auto& e : sCache)
      if (e.icao[0]) u += e.len;
    return u;
  };
  auto freeSlot = [&]() {
    for (int i = 0; i < CACHE; i++)
      if (!sCache[i].icao[0]) return i;
    return -1;
  };
  for (auto& e : sCache)  // stejné logo nahradit
    if (e.icao[0] && !strcmp(e.icao, icao)) {
      e.icao[0] = 0;
      e.len = 0;
    }
  while (freeSlot() < 0 || usedBytes() + len > POOL) {
    int lru = -1;
    for (int i = 0; i < CACHE; i++)
      if (sCache[i].icao[0] && (lru < 0 || sCache[i].used < sCache[lru].used)) lru = i;
    if (lru < 0) break;
    sCache[lru].icao[0] = 0;
    sCache[lru].len = 0;
  }
  int order[CACHE], no = 0;
  for (int i = 0; i < CACHE; i++)
    if (sCache[i].icao[0] && sCache[i].len) order[no++] = i;
  for (int a2 = 1; a2 < no; a2++)
    for (int b2 = a2; b2 > 0 && sCache[order[b2]].off < sCache[order[b2 - 1]].off; b2--) {
      int x = order[b2];
      order[b2] = order[b2 - 1];
      order[b2 - 1] = x;
    }
  int w = 0;
  for (int k = 0; k < no; k++) {
    Entry& e = sCache[order[k]];
    memmove(sPool + w, sPool + e.off, e.len);
    e.off = w;
    w += e.len;
  }
  int slot = freeSlot();
  if (slot >= 0) {
    Entry& e = sCache[slot];
    strlcpy(e.icao, icao, sizeof e.icao);
    e.found = found;
    e.used = millis();
    e.off = w;
    e.len = 0;
    if (found && len && w + len <= POOL) {
      memcpy(sPool + w, rle, len);
      e.len = len;
    }
  }
  unlock();
}

void logoService() {
  char icao[4];
  if ((int32_t)(sRetryAt - millis()) > 0) return;
  lock();
  strlcpy(icao, sPending, sizeof icao);
  sPending[0] = 0;
  unlock();
  if (!icao[0]) {
    logoStoreWeeklyCheck();  // v klidu: 1× týdně, jestli se sbírka log na GitHubu změnila
    return;
  }

  // Pracovní buffer radaru (14 kB; logo i radar běží jen v síťové úloze po sobě):
  // [0, 4096) dekódované logo / sestavení záznamu pro flash, [4096, …) RLE.
  uint8_t* scratch = radarScratch();
  uint8_t* px = scratch;
  uint8_t* rle = scratch + 4096;
  const int RLE_MAX = 2 * LOGO_SIZE * LOGO_SIZE;
  uint32_t gen = logoStoreGen();

  // 1) ve flash a ověřené v aktuální generaci -> žádné stahování
  LogoRec rec{};
  bool have = logoStoreFind(icao, rec, rle, RLE_MAX);
  if (have && rec.gen == gen) {
    cacheInsert(icao, rec.found, rle, rec.len);
    snprintf(sStatus, sizeof sStatus, "%s: %s · %d", icao, rec.found ? "flash" : "-", logoStoreCount());
    return;
  }

  // 2) stáhnout (u starší generace jen "změnilo se?" podle značky verze)
  char etag[68] = "";
  int r = fetch(icao, px, have && rec.found ? rec.etag : nullptr, etag, sizeof etag);
  if (r == 2 && have) {  // nezměněno -> jen potvrdit generaci
    rec.gen = gen;
    logoStorePut(icao, rec, rle, scratch);
    cacheInsert(icao, rec.found, rle, rec.len);
    snprintf(sStatus, sizeof sStatus, "%s: OK (304) · %d", icao, logoStoreCount());
    Serial.printf("[Logo] %s\n", sStatus);
    return;
  }
  if (r < 0) {  // síťová chyba: případně ukázat uloženou (starší) verzi, zkusit za 30 s
    if (have) cacheInsert(icao, rec.found, rle, rec.len);
    snprintf(sStatus, sizeof sStatus, "%s: %s", icao, pngNetError());
    Serial.printf("[Logo] %s\n", sStatus);
    sRetryAt = millis() + 30000;
    return;
  }
  LogoRec nr{};
  nr.found = r == 1;
  nr.gen = gen;
  nr.len = nr.found ? rleEncode(px, rle, RLE_MAX) : 0;
  strlcpy(nr.etag, etag, sizeof nr.etag);
  bool saved = logoStorePut(icao, nr, rle, scratch);  // px už není potřeba – buffer na záznam
  cacheInsert(icao, nr.found, rle, nr.len);
  snprintf(sStatus, sizeof sStatus, "%s: %s · %d", icao, nr.found ? "OK" : "logo neexistuje", logoStoreCount());
  Serial.printf("[Logo] %s (%d B%s)\n", sStatus, nr.len, saved ? ", uloženo do flash" : "");
}
