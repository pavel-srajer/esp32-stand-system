#pragma once
#include <Arduino.h>

// Nastavení uložené v NVS (flash). Mění se přes webové rozhraní.
// údaje na obrazovce Detail letadla (volací znak se zobrazuje vždy)
enum DetailBits {
  DET_LOGO = 1 << 0,
  DET_ROUTE = 1 << 1,
  DET_TYPE = 1 << 2,
  DET_ALT = 1 << 3,
  DET_FT = 1 << 4,
  DET_SPEED = 1 << 5,
  DET_DIST = 1 << 6,
  DET_INDEX = 1 << 7,
};

struct Settings {
  String ssid;
  String pass;
  float lat = 50.0755f;  // střed radaru
  float lon = 14.4378f;
  String wsi = "0-20000-0-11520";  // stanice ČHMI
  String name = "Praha-Libuš";
  int elev = 302;      // nadmořská výška stanice (přepočet tlaku)
  int rangeIdx = 1;    // dosah radaru (index do RANGES_KM)
  bool autoUpdate = true;  // automatické OTA aktualizace z GitHub Releases
  // jas displeje: pin podsvícení (-1 = nepřipojen -> softwarové stmívání obrazu)
  int blPin = -1;
  // začátky období v minutách od půlnoci a jas v %
  int tDay = 7 * 60, tEve = 20 * 60, tNight = 22 * 60 + 30;
  int bDay = 100, bEve = 60, bNight = 15;
  // zapnuté obrazovky (bit = pořadí: radar letadel, detail, předpověď, meteoradar, stanice ČHMI, hodiny)
  int screens = 0x2F;   // vše kromě měření ze stanice ČHMI
  int rainRadiusIdx = 1;  // meteoradar: index do {25, 50, 100, 200} km
  // podklad map: 0 žádný, 1 hranice a města, 2 hranice, řeky a města
  int rainMap = 2;    // meteoradar
  int planeMap = 0;   // radar letadel
  int cycleSec = 0;   // automatické přepínání obrazovek (0 = vypnuto)
  String ntp1 = "pool.ntp.org";     // hlavní NTP server
  String ntp2 = "time.google.com";  // záložní NTP server
  int lang = 0;  // jazyk: 0 čeština, 1 English, 2 Deutsch, 3 Italiano
  int detailMask = 0xFF;  // údaje na obrazovce Detail letadla (bity DET_*)
  int speedUnit = 0;      // rychlost letadla: 0 km/h, 1 uzly (kt), 2 mph
  int routeFmt = 0;       // trasa: 0 IATA, 1 ICAO, 2 město, 3 název letiště
};

extern Settings gCfg;

void settingsLoad();
void settingsSave();
void settingsSaveRange();
bool settingsHasWifi();

// Příznak "po restartu spustit konfigurační portál"
void settingsSetPortalFlag(bool on);
bool settingsTakePortalFlag();  // přečte a smaže

// Příznak "po restartu hned instalovat aktualizaci" (dokud je paměť volná)
void settingsSetOtaFlag(bool on);
bool settingsTakeOtaFlag();
