#pragma once
#include <Arduino.h>

// Předpověď počasí z Open-Meteo (zdarma, bez klíče) pro polohu domova.
// Z ní se odvozují upozornění: déšť/sníh v nejbližších hodinách, mráz, náledí,
// silný vítr, bouřky, horko.

enum AlertLevel : uint8_t { AL_INFO, AL_WARN, AL_DANGER };

struct Forecast {
  bool ok = false;
  float curT = NAN;
  int curCode = -1;
  float curWind = NAN, curGust = NAN;  // km/h
  bool isDay = true;
  // hodinová předpověď od aktuální hodiny
  static const int H = 24;
  int hHour[H];          // místní hodina
  float hT[H];
  float hPrec[H];        // mm
  uint8_t hProb[H];      // %
  int16_t hCode[H];
  float hGust[H];
  int nh = 0;
  // 15minutové srážky na nejbližší 2 h
  float m15[8];
  int nm15 = 0;
  // denní předpověď
  int dCode[3];
  float dMax[3], dMin[3], dPrec[3];
  int nd = 0;
  // upozornění
  char alert[4][56];
  uint8_t alertLevel[4];
  int nAlerts = 0;
  char nowcast[56] = "";  // krátká věta o srážkách (i pro obrazovku radaru)
  uint32_t updatedMs = 0;
  char err[64] = "";
};

bool forecastUpdate();
void forecastGet(Forecast& out);   // kopie (pod zámkem)
const char* weatherText(int code);
