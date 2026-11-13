#pragma once
// Jazyky rozhraní (displej i web). Text se vybírá podle gCfg.lang:
//   tr("česky", "English", "Deutsch", "Italiano")
#include "settings.h"

enum Lang { LANG_CS, LANG_EN, LANG_DE, LANG_IT, LANG_COUNT };

inline const char* tr(const char* cs, const char* en, const char* de, const char* it) {
  switch (gCfg.lang) {
    case LANG_EN: return en;
    case LANG_DE: return de;
    case LANG_IT: return it;
    default: return cs;
  }
}

// 8 světových stran ve zvoleném jazyce
inline const char* trCompass(int i) {
  static const char* const D[LANG_COUNT][8] = {
      {"S", "SV", "V", "JV", "J", "JZ", "Z", "SZ"},
      {"N", "NE", "E", "SE", "S", "SW", "W", "NW"},
      {"N", "NO", "O", "SO", "S", "SW", "W", "NW"},
      {"N", "NE", "E", "SE", "S", "SO", "O", "NO"},
  };
  int l = (gCfg.lang >= 0 && gCfg.lang < LANG_COUNT) ? gCfg.lang : 0;
  return D[l][i & 7];
}
