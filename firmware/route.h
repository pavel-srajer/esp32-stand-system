#pragma once
#include <Arduino.h>

// Trasa letu (odkud → kam, IATA kódy) podle volacího znaku.
// ADS-B trasu nevysílá – dohledává se v databázích:
//   1) adsb.lol routeset (POST, ověří trasu proti poloze letadla – "plausible")
//   2) adsbdb.com /v0/callsign/<znak> (záloha)

struct Airport {
  char iata[5] = "";   // "PRG"
  char icao[5] = "";   // "LKPR"
  char city[24] = "";  // "Prague"
  char name[44] = "";  // "Václav Havel Airport Prague"
};

struct RouteInfo {
  Airport from, to;
  bool plausible = true;  // false = trasa nesedí k poloze (může být zastaralá)
};

// UI: vrátí true a vyplní out, když je trasa známá. Jinak si ji vyžádá
// (stáhne se na pozadí) a vrátí false. lat/lon = poloha letadla (pro ověření).
bool routeGet(const char* callsign, float lat, float lon, RouteInfo& out);

// Síťová úloha: dohledá požadovanou trasu (pokud nějaká čeká).
void routeService();

// stav pro stránku Systém (poslední pokus)
const char* routeStatus();
