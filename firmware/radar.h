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

// Projde běhy pixelů snímku i: fn(x, y, len, rgb332) jen pro nenulové barvy.
typedef void (*RadarRunFn)(int x, int y, int len, uint8_t color, void* ctx);
void radarForEachRun(int i, RadarRunFn fn, void* ctx);
