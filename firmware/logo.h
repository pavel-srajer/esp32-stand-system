#pragma once
#include <Arduino.h>

// Loga leteckých společností podle ICAO kódu (první 3 písmena volacího znaku,
// např. CSA123 -> CSA). Zdroj: github.com/Jxck-S/airline-logos (flightaware_logos,
// 90×90 PNG). Loga jsou majetkem příslušných společností.

#define LOGO_SIZE 56

// "CSA123" -> "CSA"; vrací false, když volací znak nevypadá jako let společnosti
bool logoIcaoFromCallsign(const char* callsign, char icao[4]);

// UI: chci zobrazit logo (když ještě není, stáhne se na pozadí).
// Zkopíruje RGB332 pixely LOGO_SIZE×LOGO_SIZE do out a vrátí true, pokud je logo k dispozici.
bool logoGet(const char* icao, uint8_t* out);

// Síťová úloha: stáhne požadované logo (pokud nějaké čeká).
void logoService();
