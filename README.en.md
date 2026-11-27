# esp32-stand-system

[Česky](README.md) · **English**

A desk display built on an **ESP32-C3 Super Mini** with a round **GC9A01 1.28"** display (240×240):
nearby aircraft (ADS-B) with logo and route, weather forecast and alerts, the animated ČHMI weather radar
with an analysis of “when will it start raining at your place”, the spot electricity price and a clock with name days.
It is set up from your phone through a WiFi hotspot and updates itself from GitHub Releases (OTA).
The interface is available in **Czech, English, German, Italian, Portuguese, Spanish and Vietnamese**.

Detailed illustrated guide (7 languages, switcher at the top): [`docs/navod.html`](docs/navod.html)

> ČHMI is the Czech Hydrometeorological Institute and OTE the Czech electricity market operator. The weather station, radar and electricity price features cover the Czech Republic; aircraft, forecast and everything else work anywhere.

## Screens and controls

Everything is controlled with one **BOOT** button (GPIO 9):

- **Short press:** next enabled screen.
- **Long press (0.7–5 s):** an action depending on the screen, see the table.
- **Hold 5 s:** setup hotspot. While holding, an orange ring fills up around the edge.

| Screen | What it shows | Long press |
|---|---|---|
| **Aircraft radar** | aircraft around your home, coloured by altitude, the nearest one at the bottom | range 10 / 25 / 50 / 100 km |
| **Aircraft detail** | airline logo and name, callsign, route (PRG → LHR), type, registration, altitude, speed, track, distance. What is shown is chosen on the web. Aircraft in range can cycle automatically (interval set on the web) | next aircraft |
| **Forecast** | current weather with an icon and alerts (rain starts in X min, frost, black ice, strong wind, thunderstorms, snow, heat), outlook for 2–8 h | refresh |
| **ČHMI weather radar** | animation of precipitation over the last 30 min on an outline map. Below is an analysis, e.g. “Rain 12 km W, arrives in ~25 min”, plus the speed and direction of movement | radius 25 / 50 / 100 / 200 km |
| **ČHMI station measurements** | temperature, humidity, pressure, wind, precipitation (off by default) | refresh |
| **Clock** | time, date, name day, public holidays (including Easter), temperature, number of aircraft (only when Radar or Detail is enabled too), IP address | refresh |
| **Spot electricity price** | current quarter hour in CZK/kWh (3 decimal places), next quarter hour, chart of the day in 15-min steps, cheapest and most expensive time (off by default) | tomorrow for 30 s |
| **Cheap electricity** | up to 3 cheapest windows from your settings (e.g. washer 2 h between 8–22, car 4 h overnight): time from–to, today/tomorrow, average price. A window running right now is green (off by default) | – |
| **Note** | your own text from the System page with optional text and background colour, size, bold, italic, underline and blinking. The text wraps to fit the circle (off by default) | stops blinking |

- After power-on a **splash screen** with the firmware version is shown for 3 s. WiFi is already connecting meanwhile.
- Aircraft colours by altitude: green < 900 m, yellow < 3 km, orange < 6 km, pink < 9 km, blue higher, grey = on the ground.
- A **red dot** at the top means WiFi is not connected.
- **Alerts for interesting aircraft** (enabled on the web): emergency squawk 7500/7600/7700, military aircraft or aircraft from your list. They blink red on the radar, and the display switches to their detail with a red label (e.g. “7700 EMERGENCY”) and a blinking edge. A short press acknowledges the alert.

## First setup (hotspot)

1. On first power-on the device creates an **open WiFi `Letadla-XXXX`**. The display shows a QR code that connects your phone to it.
2. The phone should open the settings page by itself. If not, open **http://192.168.4.1**.
3. At the top choose the **language**, then your WiFi and password, your home location (centre of the radar) and the ČHMI weather station. Press **Save**.
4. The device restarts and connects to your network.

Settings are stored in flash and survive updates. You can open them later:
- in the browser at the **device IP address** (shown at the bottom of the Clock screen),
- or by holding the button for 5 s, which starts the hotspot again.

If the device fails to connect to the saved WiFi three times after power-on (about 1 min), it switches to the hotspot by itself. If nobody changes anything in the hotspot for 5 minutes, it tries the saved WiFi again.

## Web settings

| Section | What you set there |
|---|---|
| **Language** | čeština / English / Deutsch / Italiano / Português / Español / Tiếng Việt (display and web). Name days are Czech only, public holidays are translated |
| **WiFi** | network from the list of found ones, password |
| **Location** | coordinates, or a click on the map (OpenStreetMap) or an address search. The map only works when the phone is online (in your home network). The nearest ČHMI station is selected automatically. The **📍 My location** button can only give an approximate location from the IP address on the device’s `http://` page – refine it on the map or with an address. The aircraft radar, weather radar and Open-Meteo forecast are all computed for this location |
| **Screens** | which screens are shown and **in what order** (drag by the ☰ handle), aircraft radar range (10–100 km), weather radar radius (25–200 km), automatic rotation (off / 10–60 s; a button press pauses it for a minute), map background for both radars (none / borders and cities / borders, rivers and cities) |
| **Aircraft detail** | which items are shown (logo, airline name, route, type and registration, altitude in m, ft and FL, speed and track, distance and direction, number), automatic cycling of aircraft in range (off / every 3–30 s), speed units (km/h, kt, mph), route format (IATA PRG → LHR / ICAO LKPR → EGLL / city / airport name) |
| **Aircraft data source** | Automatic (own receiver → adsb.fi → adsb.lol), own receiver only, adsb.fi only or adsb.lol only. Own receiver address (adsb-feeder/ADSB.im, readsb, tar1090, ultrafeeder): an IP is enough, `:8080/data/aircraft.json` is added; for another port or path enter the full URL with `http://`. The own receiver is read every 5 s |
| **Spot electricity price** | data source (spotovaelektrina.cz or spotovky.cz; if the chosen one fails, the other is used automatically), surcharge in CZK/kWh (distribution, fees) and VAT in %. With 0 and 0 it is the bare exchange price. **Cheapest windows** (up to 3): name, length 15 min – 8 h and allowed time from–to (also over midnight; 00:00–00:00 = any time). Searched in today’s and, after 13:00, tomorrow’s prices, incl. surcharge and VAT |
| **Alerts for interesting aircraft** | emergency squawk, military aircraft, your own list (registration `OK-NEP`, type `A388`, hex `49d3c1` or callsign prefix `CSA`, comma-separated) and what happens for a new aircraft (switch to detail and blink for 15–300 s, or only mark it). Works with your own receiver and with the internet; for internet sources military aircraft come from the adsb.fi `/v2/mil` list (every 2 min). The same aircraft is announced again after 30 min at the earliest |
| **Night restart** | off / only when memory runs low (largest free RAM block below 8–32 kB) / every night, at what hour. It happens in the first minutes of the chosen hour, at the earliest one hour after power-on; the System page then shows “scheduled night restart” as the last reset reason |
| **Web interface login** | user and password (HTTP Basic). Empty password = no login. The user name is not case-sensitive, the password may contain accents. The browser does not autofill the fields. The hotspot (BOOT 5 s) never asks for a login, so a forgotten password can be removed with “Remove password” |
| **ČHMI weather station** | station for the Measurements screen (14 main ones, or your own WSI code) |
| **Display brightness** | day / evening / night with start time and brightness in %, backlight pin. At night below 30 % the first button press lights the display up for 30 s |
| **Time (NTP)** | primary and backup NTP server (default `pool.ntp.org`, `time.google.com`) |
| **Firmware** | version, automatic updates, “Check and install” button |

**Save and restart** stores the settings and restarts the device. The page waits until the device is back up (a few seconds) and returns to the settings by itself; with a password set it shows the user name to log in with. During the first setup through the hotspot the device moves to your home network and you can simply close the page.

**Brightness and backlight pin:**
- **Module with a BL/BLK pin:** connect it to a free GPIO (5, 6, 7, 20 or 21) and select it in the settings. Brightness is then controlled by PWM on the backlight.
- **Module without a BL pin** (most 7-pin ones): the backlight is always on and only the image is dimmed. A black background therefore still glows slightly.

**System information** (`http://<device IP>/system`) refreshes every 2 s. It shows:
- chip temperature, CPU load, RAM (free, minimum, largest block) and flash,
- WiFi (signal, channel, IP), uptime and the reason for the last restart,
- the status of all data sources (ADS-B, forecast, weather radar, logos, routes, electricity price), cheapest windows, interesting aircraft in range, NTP and updates,
- **own ADS-B receiver** (when it is used): aircraft with position / total, the farthest aircraft right now, today’s range record, messages per second and average signal (dBFS),
- **settings backup**: “Download backup” saves everything to a JSON file (optionally with WiFi and web passwords), “Restore from file” uploads it again (also to another device) and the device restarts. API: `GET /api/config[?secrets=1]`, `POST /api/config`.

At the top is the **Screen** section: it shows what is on the display right now and its buttons switch to any enabled screen. Below it is the **Note** field: “Save and show” stores the text (without a restart) and shows it on the display right away. You can set text and background colour, maximum size (large / medium / small; text that does not fit is made smaller), **bold**, *italic*, underline and **blinking** (slow or fast, the colours alternate). A blinking note stays on the display until you acknowledge it with the button. Colours are rounded to the display’s 256-colour palette. Characters the font cannot draw (emoji) are left out. Automatic rotation then pauses for a minute. The same data in JSON is returned by `/api/status`; you can also switch screens with `/api/screen?s=<number>`.

## Firmware updates (OTA from GitHub)

- A minute after start and then once a day the device checks `releases/latest` of this repository. The web settings also have a **“Check and install the latest version”** button.
- When it finds a newer version and **automatic updates** are enabled (default), **it restarts and downloads it right after boot**, when RAM is free for the encrypted connection to the GitHub server. The display shows “Update” with a percentage.
- The device resolves GitHub’s redirect to the file server (`release-assets.githubusercontent.com`) itself and closes the first connection, so only one encrypted connection is ever open. A failed download is retried up to three times, and during the update the web interface only answers with a short “Updating…” page.
- The flash has two program partitions. If a download is interrupted, the old version keeps running and the next attempt comes a day later.
- The repository must be **public**, because the device connects to the GitHub API without a token.
- Locally built firmware (version `dev`) is not overwritten automatically, but the button on the web still works.

### Releasing a new version

Change the number in the **`VERSION`** file (e.g. `0.11.0`) and push to `main`. You can also do it in the GitHub editor. The number must go up. GitHub Actions (`.github/workflows/firmware.yml`) then creates the tag `v0.11.0` and a Release with three files:

| File | Purpose |
|---|---|
| `firmware.bin` | update over WiFi (OTA), downloaded by the devices |
| `firmware-full.bin` | complete image for the first flash over USB (address `0x0`) |
| `esp32-stand-system-arduino-vX.Y.Z.zip` | the `firmware/` folder for the Arduino IDE with the version number and a guide |

Pushing a `vX.Y.Z` tag by hand works too. Every push to `main` is built automatically. Before the build, checks run that stop the release on error:
- `tools/check_web_js.py` – JavaScript syntax of the web pages in all 7 languages (a wrong character in a translation would break the map and controls),
- `tools/make_i18n.py --strict` – every text has a pt / es / vi translation with the same `%s`/`%d` and `firmware/i18n_extra.h` is up to date,
- `tools/make_docs.py --strict` – every paragraph of the guide is translated into all languages and `docs/navod.html` is up to date.

## Wiring

| Display | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO 0 |
| CS | GPIO 1 |
| DC | GPIO 10 |
| SDA (MOSI) | GPIO 3 |
| SCL (SCLK) | GPIO 4 |
| BL (if present) | optionally GPIO 5/6/7/20/21 |

The display pins are configured in `firmware/display.h`.

## First flash over USB

**A) Prebuilt image from a Release (easiest).** Download `firmware-full.bin` and flash it to address `0x0` in the browser via https://espressif.github.io/esptool-js/, or with:

```bash
esptool.py --chip esp32c3 write_flash 0x0 firmware-full.bin
```

**B) Arduino IDE.** Download `esp32-stand-system-arduino-vX.Y.Z.zip` from the Release, unzip it and open `firmware/firmware.ino`. The code is in the `.cpp`/`.h` files in the same folder. `fw_version.h` carries the version number, so automatic updates work too.
- Board (esp32 by Espressif 2.0.17): **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*, *Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)*, *Flash Mode: DIO*.
- Libraries: **LovyanGFX 1.2.7**, **ArduinoJson 7.2.1**, **PNGdec 1.1.7**.

**C) PlatformIO**

```bash
pio run -t upload && pio device monitor
```

If the board does not want to flash, hold BOOT, press RESET and release BOOT.

## Data sources

All of them are free and need no registration.

| Data | Source | How often |
|---|---|---|
| Aircraft (ADS-B) | only when Radar, Detail or alerts are enabled (the Clock then also shows the aircraft count; with the Clock alone no aircraft are downloaded). Own receiver in the home network (`aircraft.json`, 5 s), otherwise [adsb.fi](https://adsb.fi) (open data, personal non-commercial use), with [adsb.lol](https://adsb.lol) as backup (max. 500 requests/day without a key). A server answering 429 or 403 is skipped for 1–10 min. airplanes.live answers 403 without registration since 11/2026, so it is no longer used | 15 s |
| Airline logos | [Jxck-S/airline-logos](https://github.com/Jxck-S/airline-logos), 90×90 PNG by the ICAO code from the callsign (CSA123 → CSA) | on display; ~8 kept compressed in memory (4–16 depending on size) |
| Airline names | [Virtual Radar Server standing data](https://github.com/vradarserver/standing-data) (CC0), 5,905 airlines by ICAO code. Stored in the firmware, generated by `tools/make_airlines.py` | – |
| Flight route | [adsb.lol routeset](https://api.adsb.lol) (checks the route against the aircraft position, “?” when implausible), backup [adsbdb.com](https://www.adsbdb.com) | on display, 6 cached |
| Forecast | [Open-Meteo](https://open-meteo.com): current conditions, precipitation in 15-min steps for 2 h, hourly and daily forecast | 15 min |
| Weather radar | ČHMI, maximum reflectivity composite `maxz/png_masked`, last 6 images every 5 min. File names are computed from the time (UTC) and only new images are downloaded; the directory listing (over 300 kB) is only a fallback | 5 min |
| Station measurements | ČHMI open data, 10-minute measurements `10m-<WSI>-<YYYYMMDD>.json` | 10 min |
| Electricity price | [OTE](https://www.ote-cr.cz) day-ahead market results in CZK per 15 min via [spotovaelektrina.cz](https://spotovaelektrina.cz) or [spotovky.cz](https://spotovky.cz) (chosen in the settings, the other is the backup). Tomorrow is available around 13:00 | 30 min |
| Map | [Natural Earth](https://www.naturalearthdata.com) (borders, rivers; public domain), Czech cities from janpipek/cli-plotting-talk. Stored in the firmware | – |
| Name days | hermajan/timely, checked against xnekv03/nameday-api. Stored in the firmware | – |

**Radar analysis:** the newest image is searched for the nearest precipitation (distance, direction, intensity by colour). Comparing the oldest and newest image gives where and how fast the precipitation moves. From your home we then look against the direction of movement to see what will arrive and when, or when it will stop. The result also appears as the first alert line on the Forecast screen, where it replaces the model estimate.

## Structure

| File | Contents |
|---|---|
| `firmware/main.cpp` | ADS-B and ČHMI station downloads, screen drawing, button, brightness, splash screen |
| `firmware/portal.cpp` | hotspot, captive portal, web settings, System page, web translations |
| `firmware/radar.cpp` | ČHMI weather radar: download, crop, compression and precipitation movement analysis |
| `firmware/forecast.cpp` | Open-Meteo forecast and alerts |
| `firmware/price.cpp` | spot electricity price, cheapest windows |
| `firmware/logo.cpp`, `route.cpp` | airline logos, flight route |
| `firmware/png_net.cpp` | PNG decoding straight from the network (shared static decoder) |
| `firmware/http_body.h` | reading HTTP/1.1 responses including chunked |
| `firmware/ota.cpp` | GitHub Releases check and OTA |
| `firmware/sysinfo.cpp` | data for the System page, scheduled night restart |
| `firmware/settings.cpp` | settings in NVS, backup and restore (JSON) |
| `firmware/i18n.h` | languages: `tr("česky", "English", "Deutsch", "Italiano")`; Portuguese, Spanish and Vietnamese are looked up by the English text in `i18n_extra.h` |
| `firmware/i18n.cpp`, `i18n_extra.h` | pt / es / vi translations (lookup by English text), generated by `tools/make_i18n.py` |
| `tools/i18n_extra.json` | pt / es / vi translations keyed by the English text; `python3 tools/make_i18n.py` generates `firmware/i18n_extra.h` and lists what is missing (also checks `%s`/`%d`) |
| `tools/make_fonts.py` | display font from efontJA (LovyanGFX) + composed Vietnamese letters (`tools/viet_glyphs.py`, font format `tools/u8g2font.py`), output `firmware/fonts_cz.h` |
| `tools/check_web_js.py` | checks the web JavaScript in all languages (runs in CI, needs Node.js) |
| `docs/src/navod.cs.html` | the guide – **source** in Czech (edit it here) |
| `docs/i18n/navod.<lang>.json` | guide translations (en, de, it, pt, es, vi) per paragraph, keyed by the Czech text |
| `tools/make_docs.py` | builds `docs/navod.html` (all languages + switcher) and lists untranslated paragraphs; `--dump` prints the Czech blocks |
| `firmware/display.h` | display pins and configuration |
| `firmware/fonts_cz.h` | font with accents, generated by `tools/make_fonts.py` |
| `firmware/mapdata.h` | outline map, generated by `tools/make_map.py` |
| `firmware/namedays.h` | Czech name day calendar |
| `firmware/airlines.h` | airline names, generated by `tools/make_airlines.py` |

## Memory

The ESP32-C3 has no PSRAM and every encrypted connection needs about 45 kB of RAM. Therefore:
- the display is drawn **in thirds** into a 19 kB buffer instead of 57 kB,
- there is **one static PNG decoder** and PNGs are decoded while downloading,
- aircraft and electricity prices are read from JSON **one item at a time**,
- the radar has a **permanent work buffer**, and its images as well as airline logos (compressed, ~8 in 6.3 kB) have a **fixed place** (images back to back in a 12 kB block; in heavy rain the oldest are left out). They used to be re-allocated every 5 minutes, which fragmented the heap so badly that no block was left for an encrypted connection (“SSL – Memory allocation failed”),
- updates are downloaded **right after a restart** and always over **a single** encrypted connection (GitHub’s redirect is resolved separately),
- web pages are sent **in small chunks**, so they do not need a large free block of RAM.

On the System page watch the RAM **minimum**. If it stays above ~15 kB, everything is fine.

`fonts_cz.h` contains only the Latin characters of the efont font (all interface languages including Czech) plus composed Vietnamese letters: about 100 kB for 13 sizes and styles instead of several MB. Only thanks to that the firmware fits into the OTA partitions.

## Debugging

- **Serial console (115200)** prints WiFi and OTA status, number of aircraft, radar images, routes and logos and all ČHMI quantities (code, time, value).
- **A ČHMI station value is missing:** check the quantity code in the output and adjust it at the top of `main.cpp` if needed (`EL_TEMP`, `EL_HUM`, …).
- **Colours are inverted:** toggle `cfg.invert` in `display.h`.
- **Cannot log in to the web:** hold BOOT for 5 s, connect to the hotspot `Letadla-XXXX`, open http://192.168.4.1 (no password), tick “Remove password” and save. You will also see the saved user name there.
- **Backup before reflashing:** System page → “Download backup” (with passwords), after flashing use “Restore from file”.
