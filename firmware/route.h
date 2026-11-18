#pragma once
#include <Arduino.h>

// Trasa letu (odkud → kam, IATA kódy) podle volacího znaku.
// ADS-B trasu nevysílá – dohledává se v databázích:
//   1) adsb.lol routeset (POST, ověří trasu proti poloze letadla – "plausible")
//   2) adsbdb.com /v0/callsign/<znak> (záloha)

struct RouteInfo {
  char from[5] = "";   // IATA, např. "PRG"
  char to[5] = "";
  bool plausible = true;  // false = trasa nesedí k poloze (může být zastaralá)
};

// UI: vrátí true a vyplní out, když je trasa známá. Jinak si ji vyžádá
// (stáhne se na pozadí) a vrátí false. lat/lon = poloha letadla (pro ověření).
bool routeGet(const char* callsign, float lat, float lon, RouteInfo& out);

// Síťová úloha: dohledá požadovanou trasu (pokud nějaká čeká).
void routeService();

// stav pro stránku Systém (poslední pokus)
const char* routeStatus();
