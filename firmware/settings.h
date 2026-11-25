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
  DET_AIRLINE = 1 << 8,
};

enum AlertBits {
  ALERT_EMERG = 1 << 0,  // nouzový squawk 7500 / 7600 / 7700
  ALERT_MIL = 1 << 1,    // vojenská letadla (databáze readsb / adsb.fi)
  ALERT_WATCH = 1 << 2,  // vlastní seznam
};

#define PRICE_WINS 3
struct PriceWin {
  bool on = false;
  String name;
  int dur = 8;      // délka ve čtvrthodinách (8 = 2 h)
  int from = 0;     // povolený začátek (minuty od půlnoci)
  int to = 24 * 60; // povolený konec; menší než from = přes půlnoc
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
  String screenOrder = "012345678";  // pořadí obrazovek (číslice = index obrazovky)
  String note;                       // text pro obrazovku Poznámka
  uint32_t noteFg = 0xFFFFFF;        // barva textu poznámky (RGB)
  uint32_t noteBg = 0x000000;        // barva pozadí poznámky
  int noteSize = 0;                  // max. velikost: 0 velká, 1 střední, 2 malá (zmenší se, když se nevejde)
  int noteStyle = 0;                 // bity: 1 tučné, 2 kurzíva, 4 podtržené
  int noteBlink = 0;                 // blikání: 0 ne, 1 pomalé, 2 rychlé (do potvrzení tlačítkem)
  int rainRadiusIdx = 1;  // meteoradar: index do {25, 50, 100, 200} km
  // podklad map: 0 žádný, 1 hranice a města, 2 hranice, řeky a města
  int rainMap = 2;    // meteoradar
  int planeMap = 0;   // radar letadel
  int cycleSec = 0;   // automatické přepínání obrazovek (0 = vypnuto)
  String ntp1 = "pool.ntp.org";     // hlavní NTP server
  String ntp2 = "time.google.com";  // záložní NTP server
  int lang = 0;  // jazyk: 0 čeština, 1 English, 2 Deutsch, 3 Italiano
  int detailMask = 0x1FF;  // údaje na obrazovce Detail letadla (bity DET_*)
  int detailCycle = 0;    // Detail letadla: automaticky další letadlo každých N s (0 = vypnuto)
  int speedUnit = 0;      // rychlost letadla: 0 km/h, 1 uzly (kt), 2 mph
  int routeFmt = 0;       // trasa: 0 IATA, 1 ICAO, 2 město, 3 název letiště
  float priceFee = 0;     // cena elektřiny: přirážka Kč/kWh (distribuce, poplatky)
  float priceVat = 0;     // cena elektřiny: DPH v %
  String adsbUrl = "";    // vlastní přijímač ADS-B (IP/adresa, doplní se :8080/data/aircraft.json)
  int planeSrc = 0;       // letadla: 0 automaticky, 1 jen vlastní přijímač, 2 jen adsb.fi, 3 jen adsb.lol
  int priceSrc = 0;       // zdroj cen: 0 spotovaelektrina.cz, 1 spotovky.cz (druhý je záloha)
  // nejlevnější okna elektřiny (pračka, auto…): délka a povolený čas od–do
  PriceWin priceWin[PRICE_WINS];
  // noční restart: 0 vypnuto, 1 jen když dochází paměť, 2 každou noc
  int rbMode = 0;
  int rbHour = 4;     // v kolik hodin (místní čas)
  int rbMinKB = 16;   // "dochází paměť" = největší volný blok RAM pod touto hodnotou (kB)
  // přihlášení do webu (prázdné heslo = bez přihlášení)
  String webUser = "admin";
  String webPass = "";
  // upozornění na zajímavá letadla (bity ALERT_*) a vlastní seznam
  int alertMask = 0;
  String watchList = "";  // registrace, typy, hex nebo začátky volacích znaků oddělené čárkou
  int alertSec = 60;      // jak dlouho upozornění bliká (pokud ho nepotvrdíš tlačítkem)
};

extern Settings gCfg;

void settingsLoad();
void settingsSave();
void settingsSaveRange();
void settingsSaveNote();  // uloží jen poznámku (bez restartu)
bool settingsHasWifi();

// záloha / obnova: všechna nastavení jako JSON (hesla jen se secrets=true)
#include <ArduinoJson.h>
void settingsToJson(JsonObject o, bool secrets);
void settingsFromJson(JsonObjectConst o);  // jen klíče, které v JSON jsou

// Příznak "po restartu spustit konfigurační portál"
void settingsSetPortalFlag(bool on);
bool settingsTakePortalFlag();  // přečte a smaže

// Příznak "po restartu hned instalovat aktualizaci" (dokud je paměť volná)
void settingsSetOtaFlag(bool on);
void settingsSetOtaUrl(const String& url);  // adresa firmware.bin pro instalaci po restartu
String settingsTakeOtaUrl();                 // přečte a smaže
bool settingsTakeOtaFlag();
