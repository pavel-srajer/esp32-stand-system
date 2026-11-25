# esp32-stand-system

Stolní displej na **ESP32-C3 Super Mini** s kulatým displejem **GC9A01 1,28"** (240×240):
letadla v okolí (ADS-B) s logem a trasou, předpověď a upozornění na počasí, animovaný meteoradar ČHMI
s vyhodnocením „kdy u tebe začne pršet“, spotová cena elektřiny a hodiny se jmeninami.
Nastavuje se z telefonu přes WiFi hotspot a sám se aktualizuje z GitHub Releases (OTA).
Rozhraní je **česky, anglicky, německy, italsky, portugalsky, španělsky a vietnamsky**.

Podrobný návod s obrázky: [`docs/navod.html`](docs/navod.html)

## Obrazovky a ovládání

Ovládá se jedním tlačítkem **BOOT** (GPIO 9):

- **Krátký stisk:** další zapnutá obrazovka.
- **Dlouhý stisk (0,7–5 s):** akce podle obrazovky, viz tabulka.
- **Držet 5 s:** konfigurační hotspot. Během držení se po obvodu plní oranžový kruh.

| Obrazovka | Co ukazuje | Dlouhý stisk |
|---|---|---|
| **Radar letadel** | letadla kolem domova, barva podle výšky, dole nejbližší letadlo | dosah 10 / 25 / 50 / 100 km |
| **Detail letadla** | logo a název společnosti, volací znak, trasa (PRG → LHR), typ, registrace, výška, rychlost, kurz, vzdálenost. Co se zobrazí, se volí na webu. Letadla v dosahu se mohou automaticky střídat (interval na webu) | další letadlo |
| **Předpověď** | aktuální počasí s ikonou a upozornění (začne pršet za X min, mráz, náledí, silný vítr, bouřky, sněžení, horko), výhled na 2–8 h | obnovit |
| **Meteoradar ČHMI** | animace srážek za posledních 30 min na obrysové mapě. Dole je vyhodnocení, např. „Déšť 12 km Z, dorazí asi za 25 min“, a rychlost a směr postupu | poloměr 25 / 50 / 100 / 200 km |
| **Měření ze stanice ČHMI** | teplota, vlhkost, tlak, vítr, srážky (ve výchozím stavu vypnuté) | obnovit |
| **Hodiny** | čas, datum, kdo má svátek, státní svátky (i Velikonoce), teplota, počet letadel, IP adresa | obnovit |
| **Spotová cena elektřiny** | aktuální čtvrthodina v Kč/kWh (na 3 desetinná místa), další čtvrthodina, graf dne po 15 min, nejlevnější a nejdražší čas (ve výchozím stavu vypnutá) | zítřek na 30 s |
| **Levná elektřina** | až 3 nejlevnější okna podle nastavení (např. pračka 2 h mezi 8–22 h, auto 4 h přes noc): čas od–do, dnes/zítra, průměrná cena. Okno, které právě běží, svítí zeleně (ve výchozím stavu vypnutá) | – |
| **Poznámka** | vlastní text ze stránky Systém s volitelnou barvou textu a pozadí, velikostí, tučným písmem, kurzívou, podtržením a blikáním. Text se zalomí do kruhu (ve výchozím stavu vypnutá) | ukončí blikání |

- Po zapnutí se na 3 s ukáže **úvodní obrazovka** s verzí firmwaru. WiFi se mezitím už připojuje.
- Barvy letadel podle výšky: zelená < 900 m, žlutá < 3 km, oranžová < 6 km, růžová < 9 km, modrá výš, šedá = na zemi.
- **Červená tečka** nahoře znamená, že není připojená WiFi.
- **Upozornění na zajímavá letadla** (zapíná se na webu): nouzový squawk 7500/7600/7700, vojenská letadla nebo letadla z tvého seznamu. Na radaru blikají červeně, displej se přepne na jejich detail s červeným štítkem (např. „7700 NOUZE“) a blikajícím okrajem. Krátký stisk upozornění potvrdí.

## První nastavení (hotspot)

1. Po prvním zapnutí zařízení vytvoří **otevřenou WiFi `Letadla-XXXX`**. Na displeji je QR kód, kterým se k ní telefon připojí.
2. Telefon by měl sám otevřít stránku s nastavením. Když ne, otevři **http://192.168.4.1**.
3. Nahoře vyber **jazyk**, pak svou WiFi a heslo, polohu domova (střed radaru) a meteostanici ČHMI. Dej **Uložit**.
4. Zařízení se restartuje a připojí k síti.

Nastavení se ukládá do flash paměti a aktualizace ho nesmažou. Později ho otevřeš:
- v prohlížeči na **IP adrese zařízení** (je vidět dole na obrazovce Hodiny),
- nebo podržením tlačítka 5 s, které znovu spustí hotspot.

Když se zařízení po zapnutí třikrát nepřipojí k uložené WiFi (asi 1 min), přepne se samo do hotspotu. Pokud v hotspotu 5 minut nikdo nic nenastaví, zkusí znovu uloženou WiFi.

## Webové nastavení

| Sekce | Co se tam nastavuje |
|---|---|
| **Jazyk** | čeština / English / Deutsch / Italiano / Português / Español / Tiếng Việt (displej i web). Jmeniny jsou jen česky, státní svátky se překládají |
| **WiFi** | síť ze seznamu nalezených, heslo |
| **Poloha** | souřadnice, nebo kliknutí do mapy (OpenStreetMap) či vyhledání adresy. Mapa jde jen tehdy, když je telefon online (v domácí síti). Při kliknutí se sama vybere nejbližší stanice ČHMI |
| **Obrazovky** | které obrazovky se zobrazují a **v jakém pořadí** (přetažením za úchyt ☰), dosah radaru letadel (10–100 km), poloměr meteoradaru (25–200 km), automatické přepínání (vypnuto / 10–60 s; po stisku tlačítka minutu pauza), podklad mapy pro oba radary (žádný / hranice a města / hranice, řeky a města) |
| **Detail letadla** | které údaje se zobrazí (logo, název společnosti, trasa, typ a registrace, výška v m, ft a FL, rychlost a kurz, vzdálenost a směr, pořadí), automatické přepínání letadel v dosahu (vypnuto / každých 3–30 s), jednotky rychlosti (km/h, kt, mph), zobrazení trasy (IATA PRG → LHR / ICAO LKPR → EGLL / město / název letiště) |
| **Zdroj dat o letadlech** | Automaticky (vlastní přijímač → adsb.fi → adsb.lol), jen vlastní přijímač, jen adsb.fi nebo jen adsb.lol. Adresa vlastního přijímače (adsb-feeder/ADSB.im, readsb, tar1090, ultrafeeder): stačí IP, doplní se `:8080/data/aircraft.json`; jiný port nebo cestu zadej celou s `http://`. Vlastní přijímač se čte každých 5 s |
| **Spotová cena elektřiny** | zdroj dat (spotovaelektrina.cz nebo spotovky.cz; když zvolený nejde, použije se automaticky druhý), přirážka v Kč/kWh (distribuce, poplatky) a DPH v %. Při 0 a 0 je to holá burzovní cena. **Nejlevnější okna** (až 3): název, délka 15 min – 8 h a povolený čas od–do (i přes půlnoc; 00:00–00:00 = kdykoli). Hledá se v dnešních a po 13:00 i zítřejších cenách, s přirážkou a DPH |
| **Upozornění na zajímavá letadla** | nouzový squawk, vojenská letadla, vlastní seznam (registrace `OK-NEP`, typ `A388`, hex `49d3c1` nebo začátek volacího znaku `CSA`, oddělené čárkou) a co se stane u nového letadla (přepnout na detail a blikat 15–300 s, nebo jen označit). Funguje s vlastním přijímačem i s internetem; vojenská letadla z internetu se berou ze seznamu adsb.fi `/v2/mil` (každé 2 min). Stejné letadlo se ohlásí znovu nejdřív za 30 min |
| **Noční restart** | vypnuto / jen když dochází paměť (největší volný blok RAM pod 8–32 kB) / každou noc, v kolik hodin. Proběhne v prvních minutách zvolené hodiny, nejdřív hodinu po zapnutí; na stránce Systém je pak v řádku Poslední restart „plánovaný noční restart“ |
| **Přihlášení do webového rozhraní** | uživatel a heslo (HTTP Basic). Prázdné heslo = bez přihlášení. V hotspotu (BOOT 5 s) se přihlášení nevyžaduje, takže zapomenuté heslo jde zrušit |
| **Meteostanice ČHMI** | stanice pro obrazovku Měření (14 hlavních, nebo vlastní WSI kód) |
| **Jas displeje** | den / večer / noc se začátkem a jasem v %, pin podsvícení. V noci pod 30 % první stisk tlačítka rozsvítí displej na 30 s |
| **Čas (NTP)** | hlavní a záložní NTP server (výchozí `pool.ntp.org`, `time.google.com`) |
| **Firmware** | verze, automatické aktualizace, tlačítko „Zkontrolovat a nainstalovat“ |

**Jas a pin podsvícení:**
- **Modul s pinem BL/BLK:** připoj ho na volný GPIO (5, 6, 7, 20 nebo 21) a vyber ho v nastavení. Jas se pak řídí PWM podsvícením.
- **Modul bez pinu BL** (většina 7pinových): podsvícení svítí stále a ztmavuje se jen obraz. Černé pozadí proto dál slabě prosvítá.

**Systémové informace** (`http://<IP zařízení>/system`) se obnovují každé 2 s. Ukazují:
- teplotu čipu, vytížení CPU, RAM (volno, minimum, největší blok) a flash,
- WiFi (signál, kanál, IP), dobu běhu a důvod posledního restartu,
- stav všech zdrojů dat (ADS-B, předpověď, meteoradar, loga, trasy, cena elektřiny), nejlevnější okna, zajímavá letadla v dosahu, NTP a aktualizací,
- **vlastní přijímač ADS-B** (když se čte): letadla s polohou / celkem, nejvzdálenější letadlo teď, rekord dosahu dnes, zprávy za sekundu a průměrný signál (dBFS),
- **zálohu nastavení**: „Stáhnout zálohu“ uloží vše do souboru JSON (volitelně i s hesly WiFi a webu), „Obnovit ze souboru“ ho nahraje zpět (i do jiného zařízení) a zařízení se restartuje. API: `GET /api/config[?secrets=1]`, `POST /api/config`.

Nahoře je sekce **Obrazovka**: ukazuje, co je právě na displeji, a tlačítky přepneš na kteroukoli zapnutou obrazovku. Pod ní je pole **Poznámka**: tlačítkem „Uložit a zobrazit“ se text uloží (bez restartu) a hned se ukáže na displeji. Nastavit jde barva textu a pozadí, maximální velikost (velká / střední / malá; když se text nevejde, zmenší se), **tučné**, *kurzíva*, podtržení a **blikání** (pomalé nebo rychlé, střídají se barvy). Blikající poznámka zůstane na displeji, dokud ji nepotvrdíš stiskem tlačítka. Barvy se zaokrouhlí na 256barevnou paletu displeje. Znaky, které písmo neumí (emoji), se vynechají. Automatické přepínání se pak na minutu pozastaví. Totéž ve formátu JSON vrací `/api/status`, přepnout jde i přes `/api/screen?s=<číslo>`.

## Aktualizace firmwaru (OTA z GitHubu)

- Zařízení minutu po startu a pak jednou denně zkontroluje `releases/latest` tohoto repozitáře. Ve webovém nastavení je i tlačítko **„Zkontrolovat a nainstalovat nejnovější verzi“**.
- Když najde novější verzi a jsou zapnuté **automatické aktualizace** (výchozí stav), **restartuje se a stáhne ji hned po startu**. V tu chvíli je RAM volná pro šifrované spojení na server GitHubu. Na displeji je „Aktualizace“ s procenty.
- Přesměrování GitHubu na server se soubory (`release-assets.githubusercontent.com`) si zařízení zjistí samo a první spojení zavře. Je tak otevřené vždy jen jedno šifrované spojení. Stažení se při neúspěchu zkusí až třikrát a během aktualizace web odpovídá jen krátkou stránkou „Aktualizuji…“.
- Paměť má dva oddíly pro program. Když se stahování přeruší, běží dál stará verze a další pokus přijde za den.
- Repozitář musí být **veřejný**, protože zařízení se ke GitHub API připojuje bez tokenu.
- Lokálně sestavený firmware (verze `dev`) se automaticky nepřepisuje, tlačítko na webu ale funguje.

### Jak vydat novou verzi

Změň číslo v souboru **`VERSION`** (např. `0.11.0`) a pushni do `main`. Jde to i v editoru na GitHubu. Číslo musí stoupat. GitHub Actions (`.github/workflows/firmware.yml`) pak vytvoří tag `v0.11.0` a Release se třemi soubory:

| Soubor | K čemu |
|---|---|
| `firmware.bin` | aktualizace přes WiFi (OTA), stahují ho zařízení |
| `firmware-full.bin` | kompletní obraz pro první nahrání přes USB (adresa `0x0`) |
| `esp32-stand-system-arduino-vX.Y.Z.zip` | složka `firmware/` pro Arduino IDE s vloženým číslem verze a návodem |

Ruční push tagu `vX.Y.Z` funguje taky. Každý push do `main` se automaticky zkusí přeložit.

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
| BL (pokud je) | volitelně GPIO 5/6/7/20/21 |

Piny displeje jsou nastavené ve `firmware/display.h`.

## První nahrání přes USB

**A) Hotový obraz z Release (nejjednodušší).** Stáhni `firmware-full.bin` a nahraj ho na adresu `0x0` v prohlížeči přes https://espressif.github.io/esptool-js/, nebo příkazem:

```bash
esptool.py --chip esp32c3 write_flash 0x0 firmware-full.bin
```

**B) Arduino IDE.** Stáhni z Release `esp32-stand-system-arduino-vX.Y.Z.zip`, rozbal ho a otevři `firmware/firmware.ino`. Kód je v `.cpp`/`.h` souborech ve stejné složce. Soubor `fw_version.h` nese číslo verze, takže fungují i automatické aktualizace.
- Deska (esp32 od Espressif 2.0.17): **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*, *Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)*, *Flash Mode: DIO*.
- Knihovny: **LovyanGFX 1.2.7**, **ArduinoJson 7.2.1**, **PNGdec 1.1.7**.

**C) PlatformIO**

```bash
pio run -t upload && pio device monitor
```

Pokud se deska nechce nahrávat, podrž BOOT, stiskni RESET a BOOT pusť.

## Zdroje dat

Všechny jsou zdarma a bez registrace.

| Data | Zdroj | Jak často |
|---|---|---|
| Letadla (ADS-B) | vlastní přijímač v domácí síti (`aircraft.json`, 5 s), jinak [adsb.fi](https://adsb.fi) (open data, osobní nekomerční použití), záložně [adsb.lol](https://adsb.lol) (bez klíče max. 500 dotazů/den). Server, který odpoví 429 nebo 403, se na 1–10 min vynechá. airplanes.live od 11/2026 bez registrace odpovídá 403, proto se už nepoužívá | 15 s |
| Loga společností | [Jxck-S/airline-logos](https://github.com/Jxck-S/airline-logos), 90×90 PNG podle ICAO kódu z volacího znaku (CSA123 → CSA) | při zobrazení, 3 v paměti |
| Názvy společností | [Virtual Radar Server standing data](https://github.com/vradarserver/standing-data) (CC0), 5 905 společností podle ICAO kódu. Uložené ve firmwaru, generuje `tools/make_airlines.py` | – |
| Trasa letu | [adsb.lol routeset](https://api.adsb.lol) (ověří trasu proti poloze letadla, u nevěrohodné „?“), záložně [adsbdb.com](https://www.adsbdb.com) | při zobrazení, 6 v paměti |
| Předpověď | [Open-Meteo](https://open-meteo.com): aktuální stav, srážky po 15 min na 2 h, hodinová a denní předpověď | 15 min |
| Meteoradar | ČHMI, kompozit max. odrazivosti `maxz/png_masked`, posledních 6 snímků po 5 min | 5 min |
| Měření ze stanice | ČHMI otevřená data, 10min měření `10m-<WSI>-<YYYYMMDD>.json` | 10 min |
| Cena elektřiny | výsledky denního trhu [OTE](https://www.ote-cr.cz) v Kč po 15 min přes [spotovaelektrina.cz](https://spotovaelektrina.cz) nebo [spotovky.cz](https://spotovky.cz) (volba v nastavení, druhý zdroj je záloha). Zítřek je k dispozici kolem 13:00 | 30 min |
| Mapa | [Natural Earth](https://www.naturalearthdata.com) (hranice, řeky; volné dílo), česká města z janpipek/cli-plotting-talk. Uložená ve firmwaru | – |
| Jmeniny | hermajan/timely, ověřeno proti xnekv03/nameday-api. Uložené ve firmwaru | – |

**Vyhodnocení radaru:** v nejnovějším snímku se najdou nejbližší srážky (vzdálenost, směr, intenzita podle barvy). Porovnáním nejstaršího a nejnovějšího snímku se určí, kam a jak rychle se srážky posouvají. Z domova se pak díváme proti směru pohybu, co a kdy k nám dorazí, případně kdy přestane. Výsledek je i na prvním řádku upozornění na obrazovce Předpověď, kde nahrazuje modelový odhad.

## Struktura

| Soubor | Obsah |
|---|---|
| `firmware/main.cpp` | stahování ADS-B a ČHMI stanice, vykreslování obrazovek, tlačítko, jas, úvodní obrazovka |
| `firmware/portal.cpp` | hotspot, captive portál, webové nastavení, stránka Systém, překlady webu |
| `firmware/radar.cpp` | meteoradar ČHMI: stažení, ořez, komprese a vyhodnocení pohybu srážek |
| `firmware/forecast.cpp` | předpověď Open-Meteo a upozornění |
| `firmware/price.cpp` | spotová cena elektřiny |
| `firmware/logo.cpp`, `route.cpp` | loga společností, trasa letu |
| `firmware/png_net.cpp` | dekódování PNG přímo ze sítě (sdílený statický dekodér) |
| `firmware/http_body.h` | čtení HTTP/1.1 odpovědí včetně chunked |
| `firmware/ota.cpp` | kontrola GitHub Releases a OTA |
| `firmware/sysinfo.cpp` | data pro stránku Systém |
| `firmware/settings.cpp` | nastavení v NVS |
| `firmware/i18n.h` | jazyky: `tr("česky", "English", "Deutsch", "Italiano")`; portugalština, španělština a vietnamština se hledají podle anglického textu v `i18n_extra.h` |
| `tools/i18n_extra.json` | překlady pt / es / vi podle anglického textu; `python3 tools/make_i18n.py` z nich vygeneruje `firmware/i18n_extra.h` a vypíše, co chybí (kontroluje i `%s`/`%d`) |
| `tools/make_fonts.py` | písmo pro displej z efontJA (LovyanGFX) + složená vietnamská písmena (`tools/viet_glyphs.py`), výstup `firmware/fonts_cz.h` |
| `firmware/display.h` | piny a konfigurace displeje |
| `firmware/fonts_cz.h` | písmo s diakritikou, generuje `tools/make_fonts.py` |
| `firmware/mapdata.h` | obrysová mapa, generuje `tools/make_map.py` |
| `firmware/namedays.h` | česká jména v kalendáři |
| `firmware/airlines.h` | názvy leteckých společností, generuje `tools/make_airlines.py` |

## Paměť

ESP32-C3 nemá PSRAM a každé šifrované spojení potřebuje asi 45 kB RAM. Proto:
- se displej kreslí **po polovinách** do 28kB bufferu místo 57 kB,
- dekodér PNG je **jeden a statický** a PNG se dekóduje rovnou při stahování,
- letadla i ceny elektřiny se z JSON čtou **po jedné položce**,
- radar má **trvalý pracovní buffer**,
- aktualizace se stahuje **hned po restartu** a vždy jen **jedním** šifrovaným spojením (přesměrování GitHubu se řeší zvlášť).

Na stránce Systém sleduj u RAM hodnotu **minimum**. Když zůstává nad ~15 kB, je vše v pořádku.

`fonts_cz.h` obsahuje z písma efont jen latinku (včetně němčiny a italštiny) a češtinu: 17 kB místo 1,7 MB. Jen díky tomu se firmware vejde do OTA oddílů.

## Ladění

- **Sériová linka (115200)** vypisuje stav WiFi, OTA, počet letadel, snímky radaru, trasy a loga a všechny veličiny ČHMI (kód, čas, hodnota).
- **Chybí hodnota z ČHMI stanice:** zkontroluj ve výpisu kód veličiny a případně ho uprav nahoře v `main.cpp` (`EL_TEMP`, `EL_HUM`, …).
- **Barvy jsou obrácené:** přepni `cfg.invert` v `display.h`.
