#pragma once
#include <Arduino.h>
#include <PNGdec.h>

// Dekódování PNG přímo z HTTPS bez stažení celého souboru do RAM.
// Dekodér (~50 kB, obsahuje 32kB okno pro inflate) je jeden, staticky alokovaný
// při startu – za běhu by se do rozdrobené paměti nemusel vejít.
// Používá ho meteoradar i loga; volat jen ze síťové úlohy (ne souběžně).

// Volá se po přečtení hlavičky (rozměry známé), před dekódováním.
// Vrať false = nedekódovat.
typedef bool (*PngReadyFn)(PNG& png, void* user);

// Vrací true, když se obrázek dekódoval. httpCode = výsledek HTTP (pro hlášení chyb).
bool pngDecodeUrl(const char* url, PNG_DRAW_CALLBACK* draw, PngReadyFn ready, void* user, int& httpCode);
