# esp32-stand-system

Stolní „radar" na **ESP32-C3 Super Mini** s kulatým displejem **GC9A01 1,28"** (240×240).
Ukazuje letadla v okolí (ADS-B) a aktuální meteodata z ČHMI.

## Obrazovky

Přepínají se tlačítkem **BOOT** (GPIO 9):

| Obrazovka | Krátký stisk | Dlouhý stisk (>0,7 s) |
|---|---|---|
| **Radar**: letadla kolem domova, barva podle výšky, nejbližší dole | další obrazovka | dosah 10 / 25 / 50 / 100 km |
| **Detail letadla**: volací znak, typ, registrace, výška, rychlost, kurz, vzdálenost | další obrazovka | další letadlo (podle vzdálenosti) |
| **Počasí ČHMI**: teplota, vlhkost, tlak (přepočtený na hladinu moře), vítr, srážky dnes | další obrazovka | okamžitá aktualizace |
| **Hodiny**: čas, datum, teplota, počet letadel | další obrazovka | okamžitá aktualizace |

Barvy letadel podle výšky: zelená < 900 m, žlutá < 3 km, oranžová < 6 km, růžová < 9 km, modrá výš, šedá = na zemi.
Červená tečka nahoře znamená, že není připojená WiFi.

## Zapojení

| Displej | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO 0 |
| CS | GPIO 1 |
| DC | GPIO 10 |
| SDA (MOSI) | GPIO 3 |
| SCL (SCLK) | GPIO 4 |

Piny jsou nastavené ve `firmware/display.h`.

## Zdroje dat

- **Letadla:** [airplanes.live](https://airplanes.live) API (`/v2/point/lat/lon/radius`), při výpadku [adsb.lol](https://adsb.lol). Obě jsou zdarma, bez klíče, pro nekomerční použití. Dotaz jde jednou za 10 s.
- **Počasí:** otevřená data ČHMI, 10minutová měření:
  `https://opendata.chmi.cz/meteorology/climate/now/data/10m-<WSI>-<YYYYMMDD>.json`.
  Soubor má stovky kB, proto se parsuje průběžně ze streamu a do paměti se ukládá jen poslední hodnota každé veličiny. Stahuje se jednou za 10 min.

## Instalace

1. Zkopíruj `firmware/config.example.h` jako `firmware/config.h` a vyplň WiFi, svou polohu a stanici ČHMI.
2. Nahraj firmware jedním z těchto způsobů.

### PlatformIO (doporučeno)

```bash
pio run -t upload && pio device monitor
```

### Arduino IDE

- Otevři `firmware/firmware.ino` (kód je v `main.cpp` ve stejné složce).
- Deska: **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*, *Partition Scheme: Huge APP (3MB No OTA)*, *Flash Mode: DIO*.
- Knihovny: **LovyanGFX** (1.2.x) a **ArduinoJson** (7.x).

Plné oddíly (Huge APP) jsou potřeba kvůli fontu s českou diakritikou: firmware má asi 2,5 MB.

## Ladění

Na sériové lince (115200) se po každém stažení vypíše počet letadel a **všechny veličiny ČHMI** (kód, čas, hodnota).
Pokud se některá hodnota na displeji nezobrazuje, zkontroluj v tomto výpisu kód veličiny a případně ho uprav nahoře v `main.cpp` (`EL_TEMP`, `EL_HUM`, …).

Když se deska nepřipojí k WiFi, nech v `config.h` zapnuté `WIFI_LOW_TX_POWER 1`. Super Mini má známý problém s anténou.
