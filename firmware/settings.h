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
};

extern Settings gCfg;

void settingsLoad();
void settingsSave();
void settingsSaveRange();
bool settingsHasWifi();

// Příznak "po restartu spustit konfigurační portál"
void settingsSetPortalFlag(bool on);
bool settingsTakePortalFlag();  // přečte a smaže
