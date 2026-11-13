#pragma once
// Zkopíruj tento soubor jako config.h a uprav hodnoty.

// ---------- WiFi ----------
#define WIFI_SSID "moje-wifi"
#define WIFI_PASS "heslo"
// ESP32-C3 Super Mini má špatně navrženou anténu a s plným vysílacím výkonem
// se často nepřipojí. Snížení výkonu na 8,5 dBm to typicky vyřeší.
#define WIFI_LOW_TX_POWER 1

// ---------- Poloha (střed radaru) ----------
#define HOME_LAT 50.0755
#define HOME_LON 14.4378

// ---------- ČHMI meteostanice ----------
// WSI kód stanice. Seznam: https://opendata.chmi.cz/meteorology/climate/now/metadata/
//   0-20000-0-11520  Praha-Libuš (302 m)
//   0-20000-0-11518  Praha-Ruzyně (364 m)
//   0-20000-0-11723  Brno-Tuřany (241 m)
//   0-20000-0-11782  Ostrava-Mošnov (251 m)
#define CHMI_WSI "0-20000-0-11520"
#define CHMI_NAME "Praha-Libuš"
#define CHMI_ELEVATION_M 302   // pro přepočet tlaku na hladinu moře

// ---------- Časování ----------
#define PLANES_REFRESH_MS   10000UL    // ADS-B data (API povolují max ~1 dotaz/s)
#define WEATHER_REFRESH_MS  600000UL   // ČHMI publikuje 10min data, stačí 1x za 10 min

// Výchozí dosah radaru: index do {10, 25, 50, 100} km
#define DEFAULT_RANGE_IDX 1

// ---------- Časová zóna ----------
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
