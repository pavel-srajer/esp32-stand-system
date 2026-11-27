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
// Text poslední chyby (TLS chyba, velikost volné paměti...) pro zobrazení uživateli.
const char* pngNetError();

bool pngDecodeUrl(const char* url, PNG_DRAW_CALLBACK* draw, PngReadyFn ready, void* user, int& httpCode);

// Totéž s podmíněným dotazem: ifNoneMatch = značka verze (ETag) uloženého obrázku;
// když se na serveru nezměnil, vrátí false a httpCode 304 (nic se nestahuje).
// etagOut dostane značku verze staženého obrázku.
bool pngDecodeUrlEx(const char* url, PNG_DRAW_CALLBACK* draw, PngReadyFn ready, void* user, int& httpCode,
                    const char* ifNoneMatch, char* etagOut, size_t etagLen);
