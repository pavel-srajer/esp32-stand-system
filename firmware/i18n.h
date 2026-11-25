#pragma once
// Jazyky rozhraní (displej i web). Text se vybírá podle gCfg.lang:
//   tr("česky", "English", "Deutsch", "Italiano")
// Portugalština, španělština a vietnamština se hledají podle anglického textu
// v tabulce z tools/i18n_extra.json (tools/make_i18n.py -> i18n_extra.h, i18n.cpp).
#include "settings.h"

enum Lang { LANG_CS, LANG_EN, LANG_DE, LANG_IT, LANG_PT, LANG_ES, LANG_VI, LANG_COUNT };

// překlad anglického textu do pt/es/vi (lang = LANG_PT…); bez překladu vrátí en
const char* trExtra(const char* en, int lang);

inline const char* tr(const char* cs, const char* en, const char* de, const char* it) {
  switch (gCfg.lang) {
    case LANG_EN: return en;
    case LANG_DE: return de;
    case LANG_IT: return it;
    case LANG_PT:
    case LANG_ES:
    case LANG_VI: return trExtra(en, gCfg.lang);
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
      {"N", "NE", "E", "SE", "S", "SO", "O", "NO"},
      {"N", "NE", "E", "SE", "S", "SO", "O", "NO"},
      {"B", "ĐB", "Đ", "ĐN", "N", "TN", "T", "TB"},
  };
  int l = (gCfg.lang >= 0 && gCfg.lang < LANG_COUNT) ? gCfg.lang : 0;
  return D[l][i & 7];
}
