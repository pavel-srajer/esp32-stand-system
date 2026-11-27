#include "logo.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "png_net.h"
#include <LovyanGFX.hpp>

static const char* LOGO_URL = "https://raw.githubusercontent.com/Jxck-S/airline-logos/main/flightaware_logos/%s.png";
static const int CACHE = 3;

struct Entry {
  char icao[4];
  bool found;      // false = logo neexistuje (404), nezkoušet znovu
  uint32_t used;   // pro LRU
  uint8_t* px;     // LOGO_SIZE² RGB332 (ukazuje do sPx)
};
static Entry sCache[CACHE];
// Pevné místo pro loga: dřív se každé logo alokovalo zvlášť a halda se drobila
// (pak chyběl souvislý blok pro šifrované spojení).
static uint8_t sPx[CACHE][LOGO_SIZE * LOGO_SIZE];
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
      if (e.found && e.px) {
        memcpy(out, e.px, LOGO_SIZE * LOGO_SIZE);
        have = true;
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
static int fetch(const char* icao, uint8_t* out) {
  char url[128];
  snprintf(url, sizeof url, LOGO_URL, icao);
  Ctx c{};
  c.out = out;
  memset(out, 0, LOGO_SIZE * LOGO_SIZE);
  int code = 0;
  bool ok = pngDecodeUrl(url, drawCb, logoReady, &c, code);
  free(c.line);
  if (ok) return 1;
  return code == 404 ? 0 : -1;
}

void logoService() {
  char icao[4];
  if ((int32_t)(sRetryAt - millis()) > 0) return;
  lock();
  strlcpy(icao, sPending, sizeof icao);
  sPending[0] = 0;
  unlock();
  if (!icao[0]) return;

  // nejdéle nepoužité místo uvolnit a stahovat rovnou do něj
  lock();
  int slot = 0;
  for (int i = 1; i < CACHE; i++)
    if (sCache[i].used < sCache[slot].used) slot = i;
  sCache[slot].icao[0] = 0;
  sCache[slot].found = false;
  sCache[slot].px = nullptr;
  unlock();
  uint8_t* px = sPx[slot];
  int r = fetch(icao, px);
  if (r > 0) snprintf(sStatus, sizeof sStatus, "%s: OK", icao);
  else if (r == 0) snprintf(sStatus, sizeof sStatus, "%s: logo neexistuje", icao);
  else snprintf(sStatus, sizeof sStatus, "%s: %s", icao, pngNetError());
  Serial.printf("[Logo] %s\n", sStatus);
  if (r < 0) {  // síťová chyba: zkusí se znovu za 30 s
    sRetryAt = millis() + 30000;
    return;
  }
  if (r == 0) px = nullptr;
  lock();
  strlcpy(sCache[slot].icao, icao, sizeof sCache[slot].icao);
  sCache[slot].found = r > 0;
  sCache[slot].px = px;
  sCache[slot].used = millis();
  unlock();
}
