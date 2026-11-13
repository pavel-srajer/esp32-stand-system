#pragma once
#include <Arduino.h>

// Nastavení uložené v NVS (flash). Mění se přes webové rozhraní.
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
};

extern Settings gCfg;

void settingsLoad();
void settingsSave();
void settingsSaveRange();
bool settingsHasWifi();

// Příznak "po restartu spustit konfigurační portál"
void settingsSetPortalFlag(bool on);
bool settingsTakePortalFlag();  // přečte a smaže
