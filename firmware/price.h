#pragma once
#include <Arduino.h>

// Spotová cena elektřiny – výsledky denního trhu OTE (www.ote-cr.cz) přepočtené
// na CZK, převzaté ze spotovaelektrina.cz nebo spotovky.cz (obojí zdarma, bez klíče;
// zvolený zdroj se zkouší první, druhý slouží jako záloha). Od 1. 10. 2025 se
// obchoduje po 15 minutách: den = 96 čtvrthodin, slot = hodina*4 + minuta/15.

#define PRICE_SLOTS 96

struct PriceData {
  float today[PRICE_SLOTS];     // Kč/MWh (holá burzovní cena), NAN = neznámá
  float tomorrow[PRICE_SLOTS];
  bool todayOk = false, tomorrowOk = false;
  uint32_t updatedMs = 0;
  char err[48] = "";
  int src = -1;  // odkud data jsou: 0 spotovaelektrina.cz, 1 spotovky.cz
};

const char* priceSrcName(int src);

bool priceUpdate();            // síťová úloha: stáhne dnešek a zítřek
void priceTick();              // o půlnoci posune zítřek na dnešek
void priceGet(PriceData& out); // kopie pod zámkem

// konečná cena v Kč/kWh podle nastavení (přirážka a DPH; výchozí 0 = holá cena)
float priceFinal(float czkMwh);
