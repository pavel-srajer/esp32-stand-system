#include "png_net.h"
#include "http_body.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static PNG sPng;  // statická alokace = jistota, že se vejde

// Zdroj dat pro PNGdec: čte postupně ze sítě. PNGdec se občas vrátí o pár
// bajtů zpět (po hlavičce, na hranicích bloků), proto si pamatujeme posledních HIST bajtů.
static const int HIST = 4096;
struct NetSrc {
  BodyStream* body;
  uint8_t* hist;
  int32_t pos;  // kolik bajtů už přišlo ze sítě
};
static NetSrc* sSrc = nullptr;

static void* openCb(const char*, int32_t* size) {
  *size = 0x7FFFFFFF;  // velikost nemusíme znát
  return sSrc;
}
static void closeCb(void*) {}
static int32_t seekCb(PNGFILE* f, int32_t p) {
  f->iPos = p;
  return p;
}
static int32_t readCb(PNGFILE* f, uint8_t* buf, int32_t len) {
  NetSrc* s = (NetSrc*)f->fHandle;
  int32_t p = f->iPos, n = 0;
  while (n < len) {
    if (p < s->pos) {  // z historie
      if (s->pos - p > HIST) break;  // tak daleko zpět neumíme
      buf[n++] = s->hist[p % HIST];
      p++;
      continue;
    }
    int c = s->body->read();  // p == pos: nová data; p > pos: přeskočit dopředu
    if (c < 0) break;
    s->hist[s->pos % HIST] = (uint8_t)c;
    if (p == s->pos) {
      buf[n++] = (uint8_t)c;
      p++;
    }
    s->pos++;
  }
  f->iPos = p;
  return n;
}

bool pngDecodeUrl(const char* url, PNG_DRAW_CALLBACK* draw, PngReadyFn ready, void* user, int& httpCode) {
  httpCode = 0;
  uint8_t* hist = (uint8_t*)malloc(HIST);
  if (!hist) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  BodyStream::prepare(http);
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("esp32-stand-system/1.0");
  bool ok = false;
  if (http.begin(client, url)) {
    httpCode = http.GET();
    if (httpCode == 200) {
      BodyStream body(http.getStreamPtr(), BodyStream::isChunked(http), 15000);
      NetSrc src{&body, hist, 0};
      sSrc = &src;
      if (sPng.open("net", openCb, closeCb, readCb, seekCb, draw) == PNG_SUCCESS) {
        if (!ready || ready(sPng, user)) ok = sPng.decode(user, 0) == PNG_SUCCESS;
        sPng.close();
      }
      sSrc = nullptr;
    }
    http.end();
  }
  free(hist);
  return ok;
}
