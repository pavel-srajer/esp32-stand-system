#include "radar.h"
#include "settings.h"
#include "i18n.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "png_net.h"
#include <LovyanGFX.hpp>
#include <math.h>

// Adresář se snímky. "png_masked" = ozvy, které nedosahují k zemi, jsou kresleny bleději.
static const char* RADAR_DIR = "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/";
static const char* NAME_PREFIX = "pacz2gmaps3.z_max3d.";  // + YYYYMMDD.HHMM + ".0.png" (UTC)

// Zeměpisné okraje celého PNG (Web Mercator) a okraje skutečných dat
// (nad/vpravo je v obrázku titulek a legenda).
static const float LON_LEFT = 11.267f, LON_RIGHT = 20.770f;
static const float LAT_TOP = 52.167f, LAT_BOTTOM = 48.047f;
static const float LON_DATA_RIGHT = 19.624f, LAT_DATA_TOP = 51.458f;

struct Frame {
  char ts[14];  // "YYYYMMDD.HHMM"
  time_t t;
  uint8_t* rle;  // páry (počet, barva) – v sPool, u právě staženého dočasně v haldě
  uint16_t len;
  bool temp;     // rle je dočasná alokace (ještě nepřesunutá do sPool)
};

// Snímky leží v jednom pevném bloku za sebou bez mezer. Dřív byl každý snímek
// zvláštní alokace v haldě a každých 5 min se měnily – halda se rozdrobila a pro
// šifrované spojení (2 × ~17 kB) pak nezbyl souvislý blok ("SSL memory allocation
// failed"). Když se při silném dešti všechny snímky nevejdou, nejstarší se vynechají.
// (20 kB bylo zbytečně moc – každý kB tu chybí šifrovanému spojení.)
#define RADAR_POOL 12288  // typicky 6 snímků po 1–3 kB; při silném dešti se jich vejde méně
static uint8_t sPool[RADAR_POOL];
static Frame sFrames[RADAR_FRAMES];
static int sCount = 0;
static float sRadius = 0;
static char sErr[96] = "";
static uint32_t sUpdatedMs = 0;
static SemaphoreHandle_t sMutex = nullptr;
// Pracovní buffer 120×120 alokovaný natrvalo: za běhu (během TLS spojení) by se
// souvislých 14 kB nemuselo najít. Používá ho dekódování snímku i vyhodnocení,
// obojí běží jen v síťové úloze jedno po druhém.
static uint8_t sWork[RADAR_N * RADAR_N];
uint8_t* radarScratch() { return sWork; }

static void lock() {
  if (!sMutex) sMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(sMutex, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(sMutex); }

int radarFrameCount() { return sCount; }
time_t radarFrameTime(int i) { return (i >= 0 && i < sCount) ? sFrames[i].t : 0; }
const char* radarError() { return sErr; }
uint32_t radarUpdatedMs() { return sUpdatedMs; }

void radarForEachRun(int i, RadarRunFn fn, void* ctx) {
  lock();
  if (i >= 0 && i < sCount && sFrames[i].rle) {
    const uint8_t* p = sFrames[i].rle;
    const uint8_t* end = p + sFrames[i].len;
    int pos = 0;
    while (p + 1 < end) {
      int n = p[0];
      uint8_t c = p[1];
      p += 2;
      if (c) {  // rozdělit běh na řádky
        int x = pos % RADAR_N, y = pos / RADAR_N, left = n;
        while (left > 0) {
          int k = min(left, RADAR_N - x);
          fn(x, y, k, c, ctx);
          left -= k;
          x = 0;
          y++;
        }
      }
      pos += n;
    }
  }
  unlock();
}

static time_t tsToUtc(const char* s) {  // "YYYYMMDD.HHMM"
  int Y, M, D, h, m;
  if (sscanf(s, "%4d%2d%2d.%2d%2d", &Y, &M, &D, &h, &m) != 5) return 0;
  Y -= M <= 2;
  long era = (Y >= 0 ? Y : Y - 399) / 400;
  unsigned yoe = (unsigned)(Y - era * 400);
  unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (time_t)(era * 146097L + (long)doe - 719468L) * 86400 + h * 3600 + m * 60;
}

static void httpSetup(HTTPClient& http) {
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
}

// --- seznam souborů: projde HTML výpis adresáře a ponechá nejnovějších N časů ---
static int scanIndex(char out[][14], int maxN) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  httpSetup(http);
  if (!http.begin(client, RADAR_DIR)) return -1;
  int code = http.GET();
  if (code != 200) {
    http.end();
    snprintf(sErr, sizeof sErr, "radar HTTP %d", code);
    return -1;
  }
  BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http), 15000);
  int n = 0;
  size_t pl = strlen(NAME_PREFIX), mi = 0;
  int ch;
  while ((ch = body.read()) >= 0) {
    if (ch == NAME_PREFIX[mi]) {
      if (++mi < pl) continue;
      mi = 0;
      char ts[14];
      int k = 0;
      while (k < 13 && (ch = body.read()) >= 0) ts[k++] = ch;
      ts[k] = 0;
      if (k < 13 || ts[8] != '.') continue;
      bool ok = true;
      for (int i = 0; i < 13; i++)
        if (i != 8 && !isdigit((unsigned char)ts[i])) ok = false;
      if (!ok) continue;
      // vložit seřazeně (vzestupně), bez duplicit, max. maxN nejnovějších
      bool dup = false;
      for (int i = 0; i < n; i++)
        if (!strcmp(out[i], ts)) dup = true;
      if (dup) continue;
      if (n == maxN) {
        if (strcmp(ts, out[0]) <= 0) continue;
        for (int i = 1; i < n; i++) strcpy(out[i - 1], out[i]);
        n--;
      }
      int p = n;
      while (p > 0 && strcmp(out[p - 1], ts) > 0) {
        strcpy(out[p], out[p - 1]);
        p--;
      }
      strcpy(out[p], ts);
      n++;
    } else {
      mi = (ch == NAME_PREFIX[0]) ? 1 : 0;
    }
  }
  http.end();
  return n;
}

// --- dekódování jednoho snímku ---
struct DecodeCtx {
  PNG* png;
  uint16_t* line;
  uint8_t* frame;       // RADAR_N × RADAR_N, RGB332
  int16_t sx[RADAR_N];  // zdrojový sloupec pro výstupní x (-1 = mimo data)
  int16_t sy[RADAR_N];  // zdrojový řádek pro výstupní y
  int nextRow;          // první výstupní řádek, který ještě nebyl vyplněn
  float radiusKm;
};

static float mercY(float lat) { return logf(tanf(0.7853982f + lat * DEG_TO_RAD * 0.5f)); }

static int drawCb(PNGDRAW* d) {
  DecodeCtx* c = (DecodeCtx*)d->pUser;
  // sy je rostoucí: zpracujeme všechny výstupní řádky, které padnou na tento zdrojový
  while (c->nextRow < RADAR_N && c->sy[c->nextRow] < d->y) c->nextRow++;
  if (c->nextRow >= RADAR_N || c->sy[c->nextRow] != d->y) return 1;
  c->png->getLineAsRGB565(d, c->line, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
  while (c->nextRow < RADAR_N && c->sy[c->nextRow] == d->y) {
    uint8_t* out = c->frame + c->nextRow * RADAR_N;
    for (int x = 0; x < RADAR_N; x++) {
      if (c->sx[x] < 0) continue;
      uint16_t v = c->line[c->sx[x]];
      out[x] = lgfx::color332((v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8);
    }
    c->nextRow++;
  }
  return 1;
}

// po přečtení hlavičky: rozměry známé -> tabulky ořezu
static bool radarReady(PNG& png, void* user) {
  DecodeCtx* c = (DecodeCtx*)user;
  int W = png.getWidth(), H = png.getHeight();
  c->png = &png;
  c->nextRow = 0;
  // paměť až teď – po navázání TLS spojení (to potřebuje největší souvislé bloky)
  c->line = (uint16_t*)malloc(W * 2);
  c->frame = sWork;
  memset(sWork, 0, sizeof sWork);
  if (!c->line || !c->frame) return false;
  float cosLat = cosf(gCfg.lat * DEG_TO_RAD);
  float yT = mercY(LAT_TOP), yB = mercY(LAT_BOTTOM);
  for (int i = 0; i < RADAR_N; i++) {
    float off = ((i + 0.5f) / RADAR_N * 2 - 1) * c->radiusKm;  // km od středu
    float lon = gCfg.lon + off / (111.32f * cosLat);
    float lat = gCfg.lat - off / 110.57f;
    int x = lroundf((lon - LON_LEFT) / (LON_RIGHT - LON_LEFT) * (W - 1));
    int y = lroundf((yT - mercY(lat)) / (yT - yB) * (H - 1));
    c->sx[i] = (lon < LON_LEFT || lon > LON_DATA_RIGHT || x < 0 || x >= W) ? -1 : x;
    c->sy[i] = (lat > LAT_DATA_TOP || lat < LAT_BOTTOM || y < 0 || y >= H) ? -1 : y;
  }
  return true;
}

static bool decodeFrame(const char* ts, float radiusKm, Frame& f, int* httpCode = nullptr) {
  char url[160];
  snprintf(url, sizeof url, "%s%s%s.0.png", RADAR_DIR, NAME_PREFIX, ts);

  // PNG se dekóduje přímo ze sítě (sdílený statický dekodér, viz png_net.cpp)
  DecodeCtx* c = (DecodeCtx*)calloc(1, sizeof(DecodeCtx));
  if (!c) return false;
  c->radiusKm = radiusKm;
  int code = 0;
  bool ok = pngDecodeUrl(url, drawCb, radarReady, c, code);
  if (httpCode) *httpCode = code;
  uint8_t* frame = c->frame;
  free(c->line);
  free(c);
  if (!ok && code == 404) {  // snímek (zatím) neexistuje – není to chyba
    Serial.printf("[Radar] %s ještě není\n", ts);
    return false;
  }
  if (!ok) {
    snprintf(sErr, sizeof sErr, "%s: %s", tr("snímek", "image", "Bild", "immagine"), pngNetError());
    Serial.printf("[Radar] %s\n", sErr);
    return false;
  }

  // 3) pokud PNG nemá průhledné pozadí, nejčastější barvu bereme jako pozadí
  {
    uint16_t hist[256];
    memset(hist, 0, sizeof hist);
    for (int i = 0; i < RADAR_N * RADAR_N; i++) hist[frame[i]]++;
    int best = 1;
    for (int v = 1; v < 256; v++)
      if (hist[v] > hist[best]) best = v;
    if (hist[best] > RADAR_N * RADAR_N / 2)
      for (int i = 0; i < RADAR_N * RADAR_N; i++)
        if (frame[i] == best) frame[i] = 0;
  }

  // 4) RLE ve dvou průchodech (nejdřív délka, pak zápis) – bez velkého pomocného bufferu
  int o = 0;
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      f.rle = (uint8_t*)malloc(o);
      if (!f.rle) {
        return false;
      }
      f.len = o;
      o = 0;
    }
    for (int i = 0; i < RADAR_N * RADAR_N;) {
      uint8_t v = frame[i];
      int n = 1;
      while (i + n < RADAR_N * RADAR_N && frame[i + n] == v && n < 255) n++;
      if (pass == 1) {
        f.rle[o] = n;
        f.rle[o + 1] = v;
      }
      o += 2;
      i += n;
    }
  }
  strlcpy(f.ts, ts, sizeof f.ts);
  f.t = tsToUtc(ts);
  f.temp = true;
  Serial.printf("[Radar] %s: %d B RLE, heap %u\n", ts, o, (unsigned)ESP.getFreeHeap());
  return true;
}

// Uloží nový seznam snímků (chronologicky). Ponechané snímky (temp=false) jsou v sPool,
// nové (temp=true) v dočasné haldě. Volat pod zámkem.
static void commitFrames(Frame* fr, int n) {
  // když se nevejdou, vynechat nejstarší
  int total = 0;
  for (int i = 0; i < n; i++) total += fr[i].len;
  int first = 0;
  while (total > RADAR_POOL && first < n) {
    total -= fr[first].len;
    if (fr[first].temp) free(fr[first].rle);
    first++;
  }
  // ponechané setřást na začátek bloku (podle současné pozice, ať se nic nepřepíše)
  int idx[RADAR_FRAMES], nk = 0;
  for (int i = first; i < n; i++)
    if (!fr[i].temp) idx[nk++] = i;
  for (int a = 1; a < nk; a++)
    for (int b = a; b > 0 && fr[idx[b]].rle < fr[idx[b - 1]].rle; b--) {
      int x = idx[b];
      idx[b] = idx[b - 1];
      idx[b - 1] = x;
    }
  int w = 0;
  for (int k = 0; k < nk; k++) {
    Frame& f = fr[idx[k]];
    memmove(sPool + w, f.rle, f.len);
    f.rle = sPool + w;
    w += f.len;
  }
  // nové za ně
  for (int i = first; i < n; i++)
    if (fr[i].temp) {
      memcpy(sPool + w, fr[i].rle, fr[i].len);
      free(fr[i].rle);
      fr[i].rle = sPool + w;
      fr[i].temp = false;
      w += fr[i].len;
    }
  sCount = n - first;
  for (int i = 0; i < sCount; i++) sFrames[i] = fr[first + i];
}

// Snímky jsou po 5 minutách a jmenují se podle času (UTC), takže názvy posledních
// snímků jde spočítat. Výpis adresáře (týden snímků, přes 300 kB) se tak stahovat
// nemusí – dřív se při přerušeném čtení vybraly hodiny až dny staré snímky.
// Stahuje se jen to, co ještě nemáme (obvykle 1 snímek za 5 min).
static bool updateByTime(float radiusKm, bool radiusChanged) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // bez času nejde
  Frame next[RADAR_FRAMES];
  int nn = 0, fails = 0;
  time_t t = now - now % 300;
  // nejnovější slot ještě nemusí být zveřejněný, starší mohou výjimečně chybět
  for (int k = 0; k < RADAR_FRAMES + 4 && nn < RADAR_FRAMES; k++, t -= 300) {
    char ts[14];
    struct tm g;
    gmtime_r(&t, &g);
    strftime(ts, sizeof ts, "%Y%m%d.%H%M", &g);
    int have = -1;
    lock();
    if (!radiusChanged)
      for (int i = 0; i < sCount; i++)
        if (!strcmp(sFrames[i].ts, ts)) have = i;
    if (have >= 0) next[nn++] = sFrames[have];
    unlock();
    if (have >= 0) continue;
    Frame f{};
    int code = 0;
    if (decodeFrame(ts, radiusKm, f, &code)) next[nn++] = f;
    else if (code != 404 && ++fails >= 2) break;  // síť / paměť – nezkoušet další
  }
  if (!nn) return false;
  bool anyNew = false;
  for (int i = 0; i < nn; i++) anyNew |= next[i].temp;
  // next je od nejnovějšího; animace chce od nejstaršího
  for (int i = 0; i < nn / 2; i++) {
    Frame x = next[i];
    next[i] = next[nn - 1 - i];
    next[nn - 1 - i] = x;
  }
  lock();
  commitFrames(next, nn);
  sRadius = radiusKm;
  if (anyNew) sUpdatedMs = millis();  // jen když přibyl nový snímek
  if (!fails) sErr[0] = 0;
  int cnt = sCount;
  const char* newest = cnt ? sFrames[cnt - 1].ts : "-";
  Serial.printf("[Radar] %d snímků, nejnovější %s\n", cnt, newest);
  unlock();
  return true;
}

bool radarUpdate(float radiusKm) {
  if (updateByTime(radiusKm, fabsf(radiusKm - sRadius) > 0.1f)) return true;
  // záloha: výpis adresáře (např. při delším výpadku snímků nebo bez času)
  static char want[RADAR_FRAMES][14];
  int n = scanIndex(want, RADAR_FRAMES);
  if (n <= 0) {
    if (n == 0) strlcpy(sErr, tr("radar: žádné snímky", "radar: no images", "Radar: keine Bilder", "radar: nessuna immagine"), sizeof sErr);
    Serial.printf("[Radar] %s\n", sErr);
    return false;
  }
  bool radiusChanged = fabsf(radiusKm - sRadius) > 0.1f;

  Frame next[RADAR_FRAMES];
  int nn = 0;
  for (int i = 0; i < n; i++) {
    // už máme stažený?
    int have = -1;
    lock();
    if (!radiusChanged)
      for (int k = 0; k < sCount; k++)
        if (!strcmp(sFrames[k].ts, want[i])) have = k;
    if (have >= 0) next[nn++] = sFrames[have];  // snímek zůstává
    unlock();
    if (have >= 0) continue;
    Frame f{};
    if (decodeFrame(want[i], radiusKm, f)) next[nn++] = f;
  }

  if (!nn) return false;
  bool anyNew = false;
  for (int i = 0; i < nn; i++) anyNew |= next[i].temp;
  lock();
  commitFrames(next, nn);
  sRadius = radiusKm;
  if (anyNew) sUpdatedMs = millis();
  if (nn == n) sErr[0] = 0;
  unlock();
  return true;
}

// ===================================================================
//  Vyhodnocení: kde prší, kam se srážky pohybují, kdy dorazí
// ===================================================================
static RadarInfo sInfo;

void radarInfoGet(RadarInfo& out) {
  lock();
  out = sInfo;
  unlock();
}

// intenzita podle barvy ČHMI škály: modrá slabé -> zelená -> žlutá/oranžová -> červená/fialová/bílá
static uint8_t level332(uint8_t v) {
  if (!v) return 0;
  int r = ((v >> 5) & 7) * 36, g = ((v >> 2) & 7) * 36, b = (v & 3) * 85;
  if (r >= 180 && g >= 180 && b >= 170) return 4;  // bílá = extrémní
  if (r >= 180 && g < 110) return 4;               // červená, fialová
  if (r >= 150 && g >= 110) return 3;              // žlutá, oranžová
  if (g >= 110) return 2;                          // zelená
  return 1;                                        // modrá
}

static void rleToLevels(const Frame& f, uint8_t* out) {
  int pos = 0;
  for (int i = 0; i + 1 < f.len && pos < RADAR_N * RADAR_N; i += 2) {
    uint8_t lv = level332(f.rle[i + 1]);
    int n = min((int)f.rle[i], RADAR_N * RADAR_N - pos);
    memset(out + pos, lv, n);
    pos += n;
  }
}

static int dirIdx(float deg) { return (int)floorf(fmodf(deg + 22.5f + 720.0f, 360.0f) / 45.0f) & 7; }
static const char* dir8(float deg) { return trCompass(dirIdx(deg)); }
static const char* dir8to(float deg) {  // "k východu"
  static const char* const D[LANG_COUNT][8] = {
      {"k severu", "k SV", "k východu", "k JV", "k jihu", "k JZ", "k západu", "k SZ"},
      {"to N", "to NE", "to E", "to SE", "to S", "to SW", "to W", "to NW"},
      {"nach N", "nach NO", "nach O", "nach SO", "nach S", "nach SW", "nach W", "nach NW"},
      {"verso N", "verso NE", "verso E", "verso SE", "verso S", "verso SO", "verso O", "verso NO"},
      {"para N", "para NE", "para E", "para SE", "para S", "para SO", "para O", "para NO"},
      {"hacia el N", "hacia el NE", "hacia el E", "hacia el SE", "hacia el S", "hacia el SO", "hacia el O", "hacia el NO"},
      {"về hướng B", "về hướng ĐB", "về hướng Đ", "về hướng ĐN", "về hướng N", "về hướng TN", "về hướng T", "về hướng TB"}};
  return D[constrain(gCfg.lang, 0, LANG_COUNT - 1)][dirIdx(deg)];
}

// maximum intenzity v kruhu (px) kolem bodu
static int levelAround(const uint8_t* L, float px, float py, int r) {
  int best = 0;
  for (int y = (int)py - r; y <= (int)py + r; y++)
    for (int x = (int)px - r; x <= (int)px + r; x++) {
      if (x < 0 || y < 0 || x >= RADAR_N || y >= RADAR_N) continue;
      if ((x - px) * (x - px) + (y - py) * (y - py) > r * r + 1) continue;
      best = max(best, (int)L[y * RADAR_N + x]);
    }
  return best;
}

// posun (dx, dy) maximalizující překryv srážek: B ≈ A posunuté o (dx, dy)
static inline bool bitAt(const uint8_t* m, int i) { return (m[i >> 3] >> (i & 7)) & 1; }

// A = bitová maska starého snímku, B = intenzity nového snímku
static bool findShift(const uint8_t* A, const uint8_t* B, int& bdx, int& bdy) {
  const int S = 4, M = RADAR_N / S;  // hrubě na blocích 4×4
  static uint8_t a[M * M], b[M * M];
  int ca = 0, cb = 0;
  for (int by = 0; by < M; by++)
    for (int bx = 0; bx < M; bx++) {
      int na = 0, nb = 0;
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int i = (by * S + y) * RADAR_N + bx * S + x;
          na += bitAt(A, i);
          nb += B[i] > 0;
        }
      a[by * M + bx] = na >= 4;
      b[by * M + bx] = nb >= 4;
      ca += a[by * M + bx];
      cb += b[by * M + bx];
    }
  if (ca < 3 || cb < 3 || ca > M * M * 7 / 10) return false;  // skoro nic, nebo prší všude
  int best = -1, gx = 0, gy = 0;
  const int R = M / 2;
  for (int dy = -R; dy <= R; dy++)
    for (int dx = -R; dx <= R; dx++) {
      int sc = 0;
      for (int y = max(0, dy); y < min(M, M + dy); y++)
        for (int x = max(0, dx); x < min(M, M + dx); x++) sc += a[(y - dy) * M + x - dx] & b[y * M + x];
      // při shodě preferuj menší posun
      if (sc > best || (sc == best && dx * dx + dy * dy < gx * gx + gy * gy)) {
        best = sc;
        gx = dx;
        gy = dy;
      }
    }
  if (best < max(2, min(ca, cb) / 4)) return false;
  // zpřesnění po pixelech kolem hrubého výsledku
  int fbest = -1;
  bdx = gx * S;
  bdy = gy * S;
  for (int dy = gy * S - S; dy <= gy * S + S; dy++)
    for (int dx = gx * S - S; dx <= gx * S + S; dx++) {
      int sc = 0;
      for (int y = max(0, dy); y < min(RADAR_N, RADAR_N + dy); y++) {
        const uint8_t* rb = B + y * RADAR_N;
        int ia = (y - dy) * RADAR_N - dx;
        for (int x = max(0, dx); x < min(RADAR_N, RADAR_N + dx); x++) sc += bitAt(A, ia + x) & (rb[x] > 0);
      }
      if (sc > fbest) {
        fbest = sc;
        bdx = dx;
        bdy = dy;
      }
    }
  return true;
}

// odstraní šum: pixel bez aspoň 2 sousedů se srážkami se vynuluje (na místě, s 2 řádkovými buffery)
static void denoise(uint8_t* F) {
  uint8_t prev[RADAR_N], cur[RADAR_N];
  memset(prev, 0, RADAR_N);
  memcpy(cur, F, RADAR_N);
  for (int y = 0; y < RADAR_N; y++) {
    const uint8_t* next = y + 1 < RADAR_N ? F + (y + 1) * RADAR_N : nullptr;  // ještě neupravený řádek
    for (int x = 0; x < RADAR_N; x++) {
      if (!cur[x]) {
        F[y * RADAR_N + x] = 0;
        continue;
      }
      int nb = 0;
      for (int xx = max(0, x - 1); xx <= min(RADAR_N - 1, x + 1); xx++) {
        nb += prev[xx] > 0;
        nb += next && next[xx] > 0;
        if (xx != x) nb += cur[xx] > 0;
      }
      F[y * RADAR_N + x] = nb >= 2 ? cur[x] : 0;
    }
    memcpy(prev, cur, RADAR_N);
    if (next) memcpy(cur, next, RADAR_N);
  }
}

void radarAnalyze(float tempC) {
  RadarInfo info;
  // jeden obraz intenzit (14 kB) + bitová maska starého snímku (1,8 kB)
  uint8_t* L = sWork;
  uint8_t* O = (uint8_t*)calloc(RADAR_N * RADAR_N / 8, 1);
  if (!O) return;
  lock();
  int n = sCount;
  float R = sRadius;
  time_t t0 = n ? sFrames[0].t : 0, t1 = n ? sFrames[n - 1].t : 0;
  if (n) {
    rleToLevels(sFrames[0], L);  // nejstarší -> maska
    denoise(L);
    for (int i = 0; i < RADAR_N * RADAR_N; i++)
      if (L[i]) O[i >> 3] |= 1 << (i & 7);
    rleToLevels(sFrames[n - 1], L);  // nejnovější
  }
  unlock();
  if (!n) {
    free(O);
    return;
  }
  denoise(L);

  const float kmPx = 2 * R / RADAR_N;
  const float c = RADAR_N / 2.0f - 0.5f;  // střed = domov
  const int rHome = max(1, (int)lroundf(2.5f / kmPx));
  info.valid = true;
  info.hereLevel = levelAround(L, c, c, rHome);
  info.here = info.hereLevel > 0;

  // nejbližší srážky a nejsilnější ve výřezu
  float bestD = 1e9;
  int bx = 0, by = 0;
  for (int y = 0; y < RADAR_N; y++)
    for (int x = 0; x < RADAR_N; x++) {
      uint8_t lv = L[y * RADAR_N + x];
      if (!lv) continue;
      info.maxLevel = max(info.maxLevel, (int)lv);
      float d = hypotf(x - c, y - c);
      if (d < bestD) {
        bestD = d;
        bx = x;
        by = y;
      }
    }
  if (bestD < 1e8 && bestD * kmPx <= R) {
    info.nearKm = bestD * kmPx;
    info.nearBrg = fmodf(atan2f(bx - c, c - by) * RAD_TO_DEG + 360, 360);
    info.nearLevel = levelAround(L, bx, by, max(1, (int)(10 / kmPx)));  // nejsilnější v okolí 10 km
  }

  // pohyb mezi nejstarším a nejnovějším snímkem
  float mins = (t1 - t0) / 60.0f;
  float vx = 0, vy = 0;  // px / min
  int dx, dy;
  if (n >= 3 && mins >= 10 && findShift(O, L, dx, dy)) {
    vx = dx / mins;
    vy = dy / mins;
    info.speed = hypotf(vx, vy) * kmPx * 60;
    info.dir = fmodf(atan2f(vx, -vy) * RAD_TO_DEG + 360, 360);
    info.moving = info.speed < 150;  // nesmyslně rychlé = chyba korelace
    if (!info.moving) vx = vy = 0;
  }

  // dorazí / přestane: z domova proti směru pohybu se díváme, co k nám připluje
  if (info.moving && info.speed >= 5) {
    for (int t = 5; t <= 120; t += 5) {
      float px = c - vx * t, py = c - vy * t;
      if (px < 0 || py < 0 || px >= RADAR_N || py >= RADAR_N) break;
      int lv = levelAround(L, px, py, rHome);
      if (info.here && !lv && info.stopMin < 0) info.stopMin = t;
      if (!info.here && lv && info.arriveMin < 0) info.arriveMin = t;
      // nejsilnější srážky, které během příštích 2 h přejdou přes domov (bouřkové jádro bývá za čelem)
      if (info.arriveMin > 0) info.nearLevel = max(info.nearLevel, lv);
      if (info.here) info.hereLevel = max(info.hereLevel, info.stopMin < 0 ? lv : 0);
    }
  }

  // texty
  bool cold = !isnan(tempC) && tempC <= 1.0f;
  auto what = [&](int lv, bool) -> const char* {
    if (lv >= 4) return tr("Bouřka", "Thunderstorm", "Gewitter", "Temporale");
    if (cold) return lv >= 3 ? tr("Silné sněžení", "Heavy snow", "Starker Schneefall", "Neve forte")
                             : tr("Sněžení", "Snow", "Schneefall", "Neve");
    if (lv >= 3) return tr("Silný déšť", "Heavy rain", "Starker Regen", "Pioggia forte");
    if (lv == 2) return tr("Déšť", "Rain", "Regen", "Pioggia");
    return tr("Slabý déšť", "Light rain", "Leichter Regen", "Pioggia debole");
  };
  if (info.here) {
    if (info.stopMin > 0) snprintf(info.line1, sizeof info.line1, tr("%s u tebe, skončí asi za %d min", "%s here, ends in ~%d min", "%s hier, endet in ca. %d min", "%s qui, finisce tra ~%d min"), what(info.hereLevel, true), info.stopMin);
    else snprintf(info.line1, sizeof info.line1, tr("%s přímo u tebe", "%s right here", "%s direkt hier", "%s proprio qui"), what(info.hereLevel, true));
  } else if (info.nearKm >= 0) {
    if (info.arriveMin > 0)
      snprintf(info.line1, sizeof info.line1, tr("%s %.0f km %s, dorazí asi za %d min", "%s %.0f km %s, arrives in ~%d min", "%s %.0f km %s, kommt in ca. %d min", "%s a %.0f km %s, arriva tra ~%d min"), what(info.nearLevel, true),
               info.nearKm, dir8(info.nearBrg), info.arriveMin);
    else {
      const char* trend = "";
      if (info.moving && info.speed >= 5) {
        float ux = sinf(info.nearBrg * DEG_TO_RAD), uy = -cosf(info.nearBrg * DEG_TO_RAD);  // směr domov -> srážky
        trend = (vx * ux + vy * uy) < 0 ? tr(", blíží se", ", approaching", ", nähert sich", ", in avvicinamento")
                                        : tr(", vzdaluje se", ", moving away", ", entfernt sich", ", si allontana");
      }
      snprintf(info.line1, sizeof info.line1, tr("%s %.0f km %s%s", "%s %.0f km %s%s", "%s %.0f km %s%s", "%s a %.0f km %s%s"), what(info.nearLevel, true), info.nearKm,
               dir8(info.nearBrg), trend);
    }
  } else {
    snprintf(info.line1, sizeof info.line1, tr("Bez srážek do %.0f km", "No rain within %.0f km", "Kein Niederschlag bis %.0f km", "Nessuna pioggia entro %.0f km"), R);
  }
  if (info.moving && info.speed >= 5) snprintf(info.line2, sizeof info.line2, tr("postup %.0f km/h %s", "moving %.0f km/h %s", "Zug %.0f km/h %s", "si muove %.0f km/h %s"), info.speed, dir8to(info.dir));
  else if (info.moving && (info.here || info.nearKm >= 0)) strlcpy(info.line2, tr("srážky skoro stojí", "almost stationary", "fast stationär", "quasi ferma"), sizeof info.line2);

  Serial.printf("[Radar] %s | %s\n", info.line1, info.line2);
  free(O);
  lock();
  sInfo = info;
  unlock();
}
