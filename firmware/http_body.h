#pragma once
#include <Arduino.h>
#include <Client.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Připíše k chybové hlášce důvod, proč se spojení nenavázalo (HTTP -1):
// " (RAM: blok 9 kB)" při nedostatku paměti pro šifrování, jinak text chyby TLS.
inline void appendConnReason(char* buf, size_t n, WiFiClientSecure& c, int code) {
  if (code >= 0) return;
  char e[64] = "";
  c.lastError(e, sizeof e);
  size_t l = strlen(buf);
  if (l >= n - 1) return;
  if (strstr(e, "emory")) snprintf(buf + l, n - l, " (RAM: blok %u kB)", (unsigned)(ESP.getMaxAllocHeap() / 1024));
  else if (e[0]) snprintf(buf + l, n - l, " (%s)", e);
}

// Tělo HTTP/1.1 odpovědi jako Stream – umí "Transfer-Encoding: chunked".
// (Některé servery, např. ADS-B API za Cloudflare, HTTP/1.0 odmítají s kódem 426,
//  a HTTPClient při HTTP/1.1 vrací v getStream() surová data i s hlavičkami chunků.)
class BodyStream : public Stream {
  Client* c;
  bool chunked;
  long remain = 0;  // zbývající bajty aktuálního chunku
  bool done = false;
  int peeked = -2;
  uint8_t buf[256];
  int len = 0, pos = 0;
  uint32_t timeoutMs;

  int rawRead() {
    if (pos < len) return buf[pos++];
    uint32_t t0 = millis();
    for (;;) {
      int av = c->available();
      if (av > 0) {
        len = c->read(buf, av > (int)sizeof buf ? (int)sizeof buf : av);
        pos = 0;
        if (len > 0) return buf[pos++];
      }
      if (!c->connected() && c->available() <= 0) return -1;
      if (millis() - t0 > timeoutMs) return -1;
      delay(1);
    }
  }

  // načte řádek s velikostí dalšího chunku (prázdné řádky = CRLF za daty přeskočí)
  bool nextChunk() {
    for (;;) {
      char line[20];
      int n = 0, ch;
      while ((ch = rawRead()) >= 0 && ch != '\n')
        if (ch != '\r' && n < (int)sizeof line - 1) line[n++] = ch;
      if (ch < 0) return false;
      line[n] = 0;
      if (!n) continue;
      remain = strtol(line, nullptr, 16);
      if (remain <= 0) {
        done = true;
        return false;
      }
      return true;
    }
  }

  int readBody() {
    if (!chunked) return rawRead();
    if (done) return -1;
    if (remain == 0 && !nextChunk()) return -1;
    int ch = rawRead();
    if (ch >= 0) remain--;
    return ch;
  }

 public:
  BodyStream(Client* cl, bool isChunked, uint32_t timeout = 10000)
      : c(cl), chunked(isChunked), timeoutMs(timeout) {}

  // připraví HTTPClient: HTTP/1.1, Connection: close, sběr Transfer-Encoding
  static void prepare(HTTPClient& http) {
    static const char* hdrs[] = {"Transfer-Encoding"};
    http.setReuse(false);
    http.collectHeaders(hdrs, 1);
  }
  static bool isChunked(HTTPClient& http) {
    return http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  }

  int read() override {
    if (peeked != -2) {
      int p = peeked;
      peeked = -2;
      return p;
    }
    return readBody();
  }
  int peek() override {
    if (peeked == -2) peeked = readBody();
    return peeked;
  }
  int available() override { return (peeked >= 0 || (!done && (pos < len || c->available() > 0))) ? 1 : 0; }
  size_t write(uint8_t) override { return 0; }
};
