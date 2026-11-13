#include "radar.h"
#include "settings.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PNGdec.h>
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
  uint8_t* rle;  // páry (počet, barva)
  uint16_t len;
};

static Frame sFrames[RADAR_FRAMES];
static int sCount = 0;
static float sRadius = 0;
static char sErr[40] = "";
static uint32_t sUpdatedMs = 0;
static SemaphoreHandle_t sMutex = nullptr;

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

static bool decodeFrame(const char* ts, float radiusKm, Frame& f) {
  char url[160];
  snprintf(url, sizeof url, "%s%s%s.0.png", RADAR_DIR, NAME_PREFIX, ts);

  // 1) stáhnout PNG do RAM
  uint8_t* buf = nullptr;
  int len = 0;
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    httpSetup(http);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != 200) {
      http.end();
      snprintf(sErr, sizeof sErr, "radar PNG HTTP %d", code);
      return false;
    }
    int size = http.getSize();
    int cap = size > 0 ? size : 65536;
    if (cap > 98304 || (int)ESP.getMaxAllocHeap() < cap + 60000) {
      http.end();
      snprintf(sErr, sizeof sErr, "málo RAM (%d kB)", (int)(ESP.getMaxAllocHeap() / 1024));
      return false;
    }
    buf = (uint8_t*)malloc(cap);
    if (!buf) {
      http.end();
      return false;
    }
    BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http), 15000);
    int ch;
    while (len < cap && (ch = body.read()) >= 0) buf[len++] = ch;
    http.end();  // uvolní TLS ještě před dekódováním
  }
  if (len < 8 || memcmp(buf, "\x89PNG", 4) != 0) {
    free(buf);
    strlcpy(sErr, "radar: není PNG", sizeof sErr);
    return false;
  }

  // 2) dekódovat s ořezem kolem domova
  DecodeCtx* c = new (std::nothrow) DecodeCtx;
  PNG* png = new (std::nothrow) PNG;
  uint8_t* frame = (uint8_t*)calloc(RADAR_N * RADAR_N, 1);
  bool ok = false;
  if (c && png && frame && png->openRAM(buf, len, drawCb) == PNG_SUCCESS) {
    int W = png->getWidth(), H = png->getHeight();
    c->png = png;
    c->frame = frame;
    c->nextRow = 0;
    c->line = (uint16_t*)malloc(W * 2);
    float cosLat = cosf(gCfg.lat * DEG_TO_RAD);
    float yT = mercY(LAT_TOP), yB = mercY(LAT_BOTTOM);
    for (int i = 0; i < RADAR_N; i++) {
      float off = ((i + 0.5f) / RADAR_N * 2 - 1) * radiusKm;  // km od středu
      float lon = gCfg.lon + off / (111.32f * cosLat);
      float lat = gCfg.lat - off / 110.57f;
      int x = lroundf((lon - LON_LEFT) / (LON_RIGHT - LON_LEFT) * (W - 1));
      int y = lroundf((yT - mercY(lat)) / (yT - yB) * (H - 1));
      c->sx[i] = (lon < LON_LEFT || lon > LON_DATA_RIGHT || x < 0 || x >= W) ? -1 : x;
      c->sy[i] = (lat > LAT_DATA_TOP || lat < LAT_BOTTOM || y < 0 || y >= H) ? -1 : y;
    }
    if (c->line) {
      png->decode(c, 0);
      ok = true;
    }
    png->close();
    free(c->line);
  } else if (png) {
    strlcpy(sErr, "radar: chyba PNG", sizeof sErr);
  }
  delete png;
  delete c;
  free(buf);
  if (!ok) {
    free(frame);
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
        free(frame);
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
  free(frame);
  strlcpy(f.ts, ts, sizeof f.ts);
  f.t = tsToUtc(ts);
  Serial.printf("[Radar] %s: %d B RLE, heap %u\n", ts, o, (unsigned)ESP.getFreeHeap());
  return true;
}

bool radarUpdate(float radiusKm) {
  static char want[RADAR_FRAMES][14];
  int n = scanIndex(want, RADAR_FRAMES);
  if (n <= 0) {
    if (n == 0) strlcpy(sErr, "radar: žádné snímky", sizeof sErr);
    Serial.printf("[Radar] %s\n", sErr);
    return false;
  }
  bool radiusChanged = fabsf(radiusKm - sRadius) > 0.1f;

  Frame next[RADAR_FRAMES];
  bool kept[RADAR_FRAMES] = {};
  int nn = 0;
  for (int i = 0; i < n; i++) {
    // už máme stažený?
    int have = -1;
    lock();
    if (!radiusChanged)
      for (int k = 0; k < sCount; k++)
        if (!strcmp(sFrames[k].ts, want[i])) have = k;
    if (have >= 0) {
      next[nn++] = sFrames[have];  // snímek zůstává, jen se přesune
      kept[have] = true;
    }
    unlock();
    if (have >= 0) continue;
    Frame f{};
    if (decodeFrame(want[i], radiusKm, f)) next[nn++] = f;
  }

  lock();
  for (int k = 0; k < sCount; k++)
    if (!kept[k]) free(sFrames[k].rle);
  for (int k = 0; k < nn; k++) sFrames[k] = next[k];
  sCount = nn;
  sRadius = radiusKm;
  if (nn) {
    sUpdatedMs = millis();
    if (nn == n) sErr[0] = 0;
  }
  unlock();
  return nn > 0;
}
