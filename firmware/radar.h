#pragma once
#include <Arduino.h>

// Meteoradar ČHMI – kompozit maximální odrazivosti (PNG každých 5 min).
// Snímky se oříznou na okolí domova, zmenší na RADAR_N×RADAR_N a uloží
// komprimované (RLE, barvy RGB332), aby se jich do RAM ESP32-C3 vešlo víc.

#define RADAR_N 120          // rozlišení uloženého snímku (na displeji ×2)
#define RADAR_FRAMES 6       // počet snímků animace (6 × 5 min = 30 min)

// Síťová úloha: obnoví seznam snímků a stáhne chybějící. radiusKm = poloměr výřezu.
bool radarUpdate(float radiusKm);

int radarFrameCount();
time_t radarFrameTime(int i);  // UTC
const char* radarError();
uint32_t radarUpdatedMs();

// Vyhodnocení radaru pro polohu domova (počítá se po každé aktualizaci snímků)
struct RadarInfo {
  bool valid = false;
  bool here = false;       // srážky přímo u domova (do ~2,5 km)
  int hereLevel = 0;       // 1 slabé, 2 mírné, 3 silné, 4 velmi silné / bouřka
  float nearKm = -1;       // nejbližší srážky (když neprší doma), -1 = žádné v dosahu
  float nearBrg = 0;       // směr k nim (°)
  int nearLevel = 0;
  int maxLevel = 0;        // nejsilnější srážky ve výřezu
  bool moving = false;     // podařilo se určit pohyb
  float speed = 0;         // km/h
  float dir = 0;           // kam se srážky posouvají (°)
  int arriveMin = -1;      // za kolik minut dorazí k domovu
  int stopMin = -1;        // za kolik minut přestane (když prší doma)
  char line1[64] = "";     // hlavní věta
  char line2[48] = "";     // pohyb
};
void radarAnalyze(float tempC);      // síťová úloha, po radarUpdate()
void radarInfoGet(RadarInfo& out);

// Projde běhy pixelů snímku i: fn(x, y, len, rgb332) jen pro nenulové barvy.
typedef void (*RadarRunFn)(int x, int y, int len, uint8_t color, void* ctx);
void radarForEachRun(int i, RadarRunFn fn, void* ctx);
