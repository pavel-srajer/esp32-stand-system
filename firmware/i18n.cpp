#include "i18n.h"
#include "i18n_extra.h"
#include <string.h>

// Binární vyhledávání v tabulce seřazené podle anglického textu (strcmp = bajtově).
const char* trExtra(const char* en, int lang) {
  int i = lang - LANG_PT;
  if (i < 0 || i > 2) return en;
  int lo = 0, hi = (int)(sizeof TR_EXTRA / sizeof TR_EXTRA[0]) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(en, TR_EXTRA[mid].en);
    if (!c) return TR_EXTRA[mid].t[i];
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return en;
}
