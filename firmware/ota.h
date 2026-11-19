#pragma once
#include <Arduino.h>

// Aktualizace firmwaru z GitHub Releases.
// GitHub Actions při tagu vX.Y.Z sestaví firmware a přiloží firmware.bin k Release.
// Zařízení se dívá na .../releases/latest a pokud je tam novější verze, stáhne ji.

#define GITHUB_REPO "pavel-srajer/esp32-stand-system"

// Balíček pro Arduino IDE z GitHub Release obsahuje fw_version.h s číslem verze,
// takže i sestavení z IDE zná svou verzi (a funguje automatická aktualizace).
#if __has_include("fw_version.h")
#include "fw_version.h"
#endif
#ifndef FW_VERSION
#define FW_VERSION "dev"  // lokální build; v CI se nastaví podle git tagu
#endif

enum OtaState { OTA_IDLE, OTA_CHECKING, OTA_UPTODATE, OTA_AVAILABLE, OTA_DOWNLOADING, OTA_FAILED };

struct OtaStatus {
  volatile int state = OTA_IDLE;
  volatile int progress = 0;  // 0-100 %
  char latest[24] = "";       // poslední verze na GitHubu
  char msg[96] = "";
};

extern OtaStatus gOta;
extern volatile bool gOtaRequest;  // požadavek z webu: zkontrolovat a nainstalovat

// Zjistí poslední verzi; pokud je novější a install==true, nainstaluje ji (a restartuje).
void otaCheck(bool install);

// Stažení a instalace firmware.bin z dané adresy (po restartu, viz gOtaBootMode).
// Přesměrování GitHubu řeší sama, aby bylo otevřené vždy jen jedno TLS spojení.
void otaInstall(const String& url);

// true = zařízení bylo restartováno kvůli aktualizaci; stahuje se hned po startu,
// než se paměť zaplní radarem, předpovědí a dalšími daty
extern bool gOtaBootMode;
