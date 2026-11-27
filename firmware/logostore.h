#pragma once
#include <Arduino.h>

// Trvalé úložiště log ve flash (oddíl "spiffs", 128 kB = 32 sektorů po 4 kB).
// Loga přežijí restart i vypnutí. Ukládají se komprimovaná (RLE, jako v RAM cache)
// do dílků po 512 B – vejde se ~120 log. Když dojde místo, smaže se nejstarší
// sektor (pár log) a ta se případně stáhnou znovu.
//
// Volat jen ze síťové úlohy (stahování log i web běží tam).

struct LogoRec {
  bool found;        // false = logo na serveru neexistuje (404)
  uint32_t gen;      // generace sbírky, ve které bylo logo ověřené
  char etag[68];     // značka verze ze serveru (If-None-Match)
  uint16_t len;      // délka RLE dat
};

// Najde logo; RLE data zkopíruje do out (max maxLen). Vrací false, když není.
bool logoStoreFind(const char* icao, LogoRec& rec, uint8_t* out, int maxLen);

// Uloží (nahradí) logo. buf = pracovní buffer aspoň 4 kB (na sestavení záznamu),
// nesmí se překrývat s rle. Vrací false, když se záznam nevejde do sektoru.
bool logoStorePut(const char* icao, const LogoRec& rec, const uint8_t* rle, uint8_t* buf);

// Generace sbírky: zvýší se, když se loga na GitHubu změnila (týdenní kontrola).
uint32_t logoStoreGen();
void logoStoreBumpGen();

// Kontrola změn sbírky log (1× týdně, jeden malý dotaz na GitHub API).
// Vrací true, když proběhla (úspěšně nebo ne) – pak už se tento týden nevolá.
bool logoStoreWeeklyCheck();

// Počet uložených log (pro stránku Systém).
int logoStoreCount();
