# esp32-stand-system

Stolní „radar" na **ESP32-C3 Super Mini** s kulatým displejem **GC9A01 1,28"** (240×240).
Ukazuje letadla v okolí (ADS-B) a aktuální meteodata z ČHMI. Nastavuje se přes WiFi hotspot a umí se sám aktualizovat z GitHub Releases (OTA).

## Ovládání tlačítkem BOOT (GPIO 9)

| Obrazovka | Krátký stisk | Dlouhý stisk (0,7–5 s) |
|---|---|---|
| **Radar**: letadla kolem domova, barva podle výšky, nejbližší dole | další obrazovka | dosah 10 / 25 / 50 / 100 km |
| **Detail letadla**: volací znak, typ, registrace, výška, rychlost, kurz, vzdálenost | další obrazovka | další letadlo (podle vzdálenosti) |
| **Počasí ČHMI**: teplota, vlhkost, tlak (přepočtený na hladinu moře), vítr, srážky dnes | další obrazovka | okamžitá aktualizace |
| **Hodiny**: čas, datum, teplota, počet letadel, IP adresa | další obrazovka | okamžitá aktualizace |

**Když tlačítko podržíš 5 s**, spustí se konfigurační hotspot. Během držení se po obvodu displeje plní oranžový kruh.

Barvy letadel podle výšky: zelená < 900 m, žlutá < 3 km, oranžová < 6 km, růžová < 9 km, modrá výš, šedá = na zemi.
Červená tečka nahoře znamená, že není připojená WiFi.

## První nastavení (hotspot)

1. Po prvním zapnutí zařízení vytvoří **otevřenou WiFi `Letadla-XXXX`**. Na displeji je QR kód, kterým se k ní telefon připojí.
2. Telefon by měl sám otevřít stránku s nastavením. Když ne, otevři **http://192.168.4.1**.
3. Vyber svou WiFi a zadej heslo, souřadnice domova (střed radaru) a meteostanici ČHMI. Pak dej **Uložit**.
4. Zařízení se restartuje a připojí k síti.

Nastavení se ukládá do flash paměti a aktualizace firmwaru ho nesmažou. Později ho změníš dvěma způsoby:
- v prohlížeči na **IP adrese zařízení** (je vidět na obrazovce hodin),
- podržením tlačítka 5 s, které znovu spustí hotspot.

Když se zařízení po zapnutí třikrát nepřipojí k uložené WiFi (asi 1 min), přepne se samo do hotspotu. Pokud v hotspotu 5 minut nikdo nic nenastaví, zkusí znovu uloženou WiFi.

## Aktualizace firmwaru (OTA z GitHubu)

- Zařízení minutu po startu a pak jednou denně zkontroluje `releases/latest` tohoto repozitáře.
  Je-li tam novější verze a v nastavení jsou zapnuté **automatické aktualizace** (výchozí stav), stáhne `firmware.bin`, nainstaluje ho a restartuje se. Na displeji se mezitím ukazuje průběh.
- Ve webovém nastavení je tlačítko **„Zkontrolovat a nainstalovat nejnovější verzi"**.
- Repozitář musí být **veřejný**, protože zařízení se ke GitHub API připojuje bez tokenu.
- Lokálně sestavený firmware (verze `dev`) se automaticky nepřepisuje, ale tlačítko na webu funguje i u něj.

### Jak vydat novou verzi

```bash
git tag v0.2.0
```

```bash
git push origin v0.2.0
```

GitHub Actions (`.github/workflows/firmware.yml`) firmware sestaví s verzí z tagu a vytvoří Release se dvěma soubory:
- `firmware.bin` stahují zařízení přes OTA,
- `firmware-full.bin` je kompletní obraz pro první nahrání přes USB (adresa `0x0`).

Číslo verze musí ve formátu `vX.Y.Z` stoupat.

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

## První nahrání přes USB

Nahrát jde jednou ze tří cest.

**A) Hotový obraz z Release.** Stáhni `firmware-full.bin` a nahraj ho:

```bash
esptool.py --chip esp32c3 write_flash 0x0 firmware-full.bin
```

Případně použij webový flasher (např. https://espressif.github.io/esptool-js/) s adresou `0x0`.

**B) PlatformIO**

```bash
pio run -t upload && pio device monitor
```

**C) Arduino IDE**
- Otevři `firmware/firmware.ino` (kód je v `.cpp` souborech ve stejné složce).
- Deska: **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*, *Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)*, *Flash Mode: DIO*.
- Knihovny: **LovyanGFX** (1.2.x) a **ArduinoJson** (7.x).

Pokud se deska nechce nahrávat, podrž BOOT, stiskni RESET a BOOT pusť.

## Zdroje dat

- **Letadla:** [airplanes.live](https://airplanes.live) API (`/v2/point/lat/lon/radius`), při výpadku [adsb.lol](https://adsb.lol). Obě jsou zdarma, bez klíče, pro nekomerční použití. Dotaz jde jednou za 10 s.
- **Počasí:** otevřená data ČHMI, 10minutová měření:
  `https://opendata.chmi.cz/meteorology/climate/now/data/10m-<WSI>-<YYYYMMDD>.json`.
  Soubor má stovky kB, proto se parsuje průběžně ze streamu a v paměti zůstane jen poslední hodnota každé veličiny. Stahuje se jednou za 10 min.

## Struktura

| Soubor | Obsah |
|---|---|
| `firmware/main.cpp` | stahování dat, vykreslování, tlačítko |
| `firmware/portal.cpp` | hotspot, captive portál, webové nastavení, seznam stanic ČHMI |
| `firmware/ota.cpp` | kontrola GitHub Releases a OTA |
| `firmware/settings.cpp` | nastavení v NVS |
| `firmware/display.h` | piny a konfigurace displeje |
| `firmware/fonts_cz.h` | písmo s diakritikou, generuje `tools/make_fonts.py` |

`fonts_cz.h` obsahuje z písma efont jen latinku a češtinu (14 kB místo 1,5 MB). Jen díky tomu se firmware vejde do OTA oddílů.

## Ladění

Na sériové lince (115200) se vypisuje stav WiFi, OTA, počet letadel a **všechny veličiny ČHMI** (kód, čas, hodnota).
Pokud se některá hodnota na displeji nezobrazuje, zkontroluj v tomto výpisu kód veličiny a případně ho uprav nahoře v `main.cpp` (`EL_TEMP`, `EL_HUM`, …).
Pokud jsou barvy obrácené, přepni `cfg.invert` v `display.h`.
