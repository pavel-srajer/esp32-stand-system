#include "portal.h"
#include "settings.h"
#include "ota.h"
#include "sysinfo.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>
#include <algorithm>

static WebServer server(80);
static DNSServer dns;
static bool gAp = false;
static uint32_t gLastActivity = 0;

struct Net {
  String ssid;
  int rssi;
  bool open;
};
static std::vector<Net> gNets;

// Hlavní stanice ČHMI (WSI, název, nadmořská výška).
// Úplný seznam: https://opendata.chmi.cz/meteorology/climate/now/metadata/
struct Station {
  const char* wsi;
  const char* name;
  int elev;
};
static const Station STATIONS[] = {
    {"0-20000-0-11518", "Praha-Ruzyně", 364},
    {"0-20000-0-11520", "Praha-Libuš", 302},
    {"0-20000-0-11406", "Cheb", 471},
    {"0-20000-0-11414", "Karlovy Vary", 603},
    {"0-20000-0-11433", "Kopisty", 240},
    {"0-20000-0-11464", "Milešovka", 833},
    {"0-20000-0-11487", "Kocelovice", 519},
    {"0-20000-0-11546", "České Budějovice", 388},
    {"0-20000-0-11636", "Churáňov", 1118},
    {"0-20000-0-11603", "Liberec", 398},
    {"0-20000-0-11723", "Brno-Tuřany", 241},
    {"0-20000-0-11698", "Kuchařovice", 334},
    {"0-20000-0-11782", "Ostrava-Mošnov", 251},
    {"0-20000-0-11787", "Lysá hora", 1322},
};
static const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

String apName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof b, "Letadla-%02X%02X", mac[4], mac[5]);
  return String(b);
}

uint32_t webLastActivity() { return gLastActivity; }

static void scan() {
  gNets.clear();
  int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (auto& e : gNets)
      if (e.ssid == s) {
        dup = true;
        e.rssi = max(e.rssi, (int)WiFi.RSSI(i));
      }
    if (!dup) gNets.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(gNets.begin(), gNets.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
  Serial.printf("[Portal] nalezeno %d sítí\n", (int)gNets.size());
}

static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += F("&amp;"); break;
      case '<': o += F("&lt;"); break;
      case '>': o += F("&gt;"); break;
      case '"': o += F("&quot;"); break;
      case '\'': o += F("&#39;"); break;
      default: o += c;
    }
  }
  return o;
}

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="cs"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nastavení – Letadla</title><style>
body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px}
main{max-width:460px;margin:auto}h1{font-size:1.4em;margin:.2em 0 .8em}
fieldset{border:1px solid #30363d;border-radius:10px;margin:0 0 16px;padding:12px 14px}
legend{color:#58a6ff;padding:0 6px}label{display:block;margin:10px 0 4px;font-size:.9em;color:#9da7b3}
input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#e6edf3;font-size:1em}
.row{display:flex;gap:10px}.row>div{flex:1}small{color:#7d8590}
button{width:100%;padding:14px;border:0;border-radius:10px;background:#238636;color:#fff;font-size:1.1em;margin-top:6px}
a{color:#58a6ff}.hide{display:none}</style></head><body><main>
<h1>✈ Letadla &amp; ČHMI – nastavení</h1>
<p><a href="/system">Systémové informace →</a></p><form method="post" action="/save">)HTML";

static const char PAGE_TAIL[] PROGMEM = R"HTML(<button type="submit">Uložit a restartovat</button></form>
<p><small>Nastavení se dá později otevřít i na IP adrese zařízení v domácí síti,
nebo podržením tlačítka BOOT 5 s spustit tento hotspot znovu.</small></p></main>
<script>
var s=document.getElementById('st'),c=document.getElementById('custom');
function u(){c.className=s.value=='-1'?'':'hide'}s.onchange=u;u();
var n=document.getElementById('ssid_sel'),m=document.getElementById('ssid');
n.onchange=function(){if(n.value)m.value=n.value;};
</script></body></html>)HTML";

static const char PAGE_SYS[] PROGMEM = R"HTML(<!doctype html><html lang="cs"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Systém – Letadla</title><style>
body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px}
main{max-width:720px;margin:auto}h1{font-size:1.4em;margin:.2em 0 .3em}a{color:#58a6ff}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin:14px 0}
.card{border:1px solid #30363d;border-radius:10px;padding:12px 14px;background:#161b22}
.lbl{font-size:.75em;letter-spacing:.08em;text-transform:uppercase;color:#7d8590}
.big{font-size:1.9em;font-weight:600;font-variant-numeric:tabular-nums;margin:2px 0}
.sub{font-size:.85em;color:#9da7b3;font-variant-numeric:tabular-nums}
.bar{height:8px;border-radius:4px;background:#30363d;overflow:hidden;margin:8px 0 4px}
.bar i{display:block;height:100%;background:#3fb950;transition:width .4s}
.bar i.w{background:#d29922}.bar i.c{background:#f85149}
table{width:100%;border-collapse:collapse;font-size:.92em}td{padding:6px 4px;border-bottom:1px solid #21262d;vertical-align:top}
td:first-child{color:#9da7b3;width:42%}h2{font-size:1.05em;margin:18px 0 6px;color:#58a6ff}
#st{font-size:.8em;color:#7d8590}</style></head><body><main>
<p><a href="/">← Nastavení</a></p><h1>Systémové informace</h1><div id="st">načítám…</div>
<div class="grid">
<div class="card"><div class="lbl">Teplota čipu</div><div class="big" id="temp">–</div><div class="bar"><i id="tempb"></i></div><div class="sub">vnitřní senzor, ±2 °C</div></div>
<div class="card"><div class="lbl">Vytížení CPU</div><div class="big" id="cpu">–</div><div class="bar"><i id="cpub"></i></div><div class="sub" id="cpus"></div></div>
<div class="card"><div class="lbl">RAM (heap)</div><div class="big" id="ram">–</div><div class="bar"><i id="ramb"></i></div><div class="sub" id="rams"></div></div>
<div class="card"><div class="lbl">Flash – firmware</div><div class="big" id="fl">–</div><div class="bar"><i id="flb"></i></div><div class="sub" id="fls"></div></div>
</div>
<h2>Zařízení</h2><table id="dev"></table>
<h2>WiFi</h2><table id="wifi"></table>
<h2>Aplikace</h2><table id="app"></table>
</main><script>
function kb(b){return b>=1048576?(b/1048576).toFixed(2)+' MB':(b/1024).toFixed(1)+' kB'}
function bar(id,p,w,c){var e=document.getElementById(id);e.style.width=Math.max(0,Math.min(100,p))+'%';e.className=p>=c?'c':p>=w?'w':''}
function up(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+' d ':'')+h+' h '+m+' min'}
function age(s){return s<0?'zatím ne':s<60?'před '+s+' s':'před '+Math.round(s/60)+' min'}
function rows(id,r){document.getElementById(id).innerHTML=r.map(function(x){return '<tr><td>'+x[0]+'</td><td>'+x[1]+'</td></tr>'}).join('')}
function t(id,v){document.getElementById(id).textContent=v}
function esc(v){return String(v).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function load(){fetch('/api/status').then(function(r){return r.json()}).then(function(j){
 t('temp',j.temp==null?'–':String(j.temp).replace('.',',')+' °C');bar('tempb',(j.temp||0)/90*100,60,75);
 t('cpu',j.cpu<0?'–':j.cpu+' %');bar('cpub',j.cpu,60,85);t('cpus',j.mhz+' MHz, 1 jádro (RISC-V)');
 var h=j.heap,used=h.total-h.free;t('ram',Math.round(used/h.total*100)+' %');bar('ramb',used/h.total*100,75,90);
 t('rams','volno '+kb(h.free)+' z '+kb(h.total)+' · minimum '+kb(h.min)+' · největší blok '+kb(h.block));
 var f=j.flash;t('fl',Math.round(f.used/f.slot*100)+' %');bar('flb',f.used/f.slot*100,80,92);
 t('fls',kb(f.used)+' z '+kb(f.slot)+' (oddíl '+f.part+')');
 rows('dev',[['Firmware',esc(j.fw)],['Sestaveno',esc(j.build)],['Čip',esc(j.chip)+' rev '+j.rev+', '+j.mhz+' MHz'],
  ['Flash celkem',kb(f.size)+', '+f.mhz+' MHz'],['Nastavení (NVS)',f.nvs_used+' / '+f.nvs_total+' položek'],
  ['Běží',up(j.uptime)],['Poslední restart',esc(j.reset)]]);
 var w=j.wifi;rows('wifi',[['Režim',esc(w.mode)+(w.connected?', připojeno':'')],['Síť',esc(w.ssid||'–')],
  ['Signál',w.connected?w.rssi+' dBm':'–'],['Kanál',w.ch],['IP adresa',esc(w.ip)],['MAC',esc(w.mac)]].concat(w.mode=='hotspot'?[['Připojení klienti',w.clients]]:[]));
 var a=j.app;rows('app',[['Letadla v dosahu',a.planes+' (dosah '+a.range+' km)'],['ADS-B data',age(a.planes_age)+(a.planes_err?' · chyba: '+esc(a.planes_err):'')],
  ['Počasí ČHMI',age(a.weather_age)+(a.weather_err?' · chyba: '+esc(a.weather_err):'')],['Stanice',esc(a.station)],
  ['Aktualizace',esc(a.ota)],['Volný zásobník',a.stack_net+' B (síť), '+a.stack_loop+' B (displej)']]);
 t('st','aktualizováno '+new Date().toLocaleTimeString('cs-CZ'));
}).catch(function(){t('st','zařízení neodpovídá')})}
load();setInterval(load,2000);
</script></body></html>)HTML";

static void handleSystem() {
  gLastActivity = millis();
  server.send_P(200, "text/html; charset=utf-8", PAGE_SYS);
}

static void handleStatus() {
  gLastActivity = millis();
  JsonDocument doc;
  sysinfoJson(doc.to<JsonObject>());
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void handleRoot() {
  gLastActivity = millis();
  if (server.hasArg("rescan")) scan();

  String h;
  h.reserve(6000);
  h += FPSTR(PAGE_HEAD);

  // --- WiFi ---
  h += F("<fieldset><legend>WiFi</legend>");
  if (!gAp && WiFi.status() == WL_CONNECTED) {
    h += F("<p><small>Připojeno k <b>");
    h += esc(WiFi.SSID());
    h += F("</b>, IP ");
    h += WiFi.localIP().toString();
    h += F("</small></p>");
  }
  h += F("<label>Dostupné sítě (<a href='/?rescan=1'>vyhledat znovu</a>)</label><select id='ssid_sel'>"
         "<option value=''>— vyber síť —</option>");
  for (auto& n : gNets) {
    h += F("<option value=\"");
    h += esc(n.ssid);
    h += "\"";
    if (n.ssid == gCfg.ssid) h += F(" selected");
    h += ">";
    h += esc(n.ssid);
    h += F(" (");
    h += String(constrain(2 * (n.rssi + 100), 0, 100));
    h += F(" %");
    if (n.open) h += F(", otevřená");
    h += F(")</option>");
  }
  h += F("</select><label>Název sítě (SSID)</label><input id='ssid' name='ssid' required value=\"");
  h += esc(gCfg.ssid);
  h += F("\"><label>Heslo</label><input name='pass' type='password' autocomplete='off' placeholder='");
  h += gCfg.pass.length() ? F("beze změny") : F("prázdné = otevřená síť");
  h += F("'></fieldset>");

  // --- Poloha ---
  h += F("<fieldset><legend>Poloha (střed radaru)</legend><div class='row'><div><label>Zeměpisná šířka</label>"
         "<input name='lat' inputmode='decimal' required value='");
  h += String(gCfg.lat, 5);
  h += F("'></div><div><label>Zeměpisná délka</label><input name='lon' inputmode='decimal' required value='");
  h += String(gCfg.lon, 5);
  h += F("'></div></div><small>Např. na mapy.cz: pravé tlačítko na místo → souřadnice (50.0755N, 14.4378E).</small>"
         "</fieldset>");

  // --- ČHMI ---
  int sel = -1;
  for (int i = 0; i < NUM_STATIONS; i++)
    if (gCfg.wsi == STATIONS[i].wsi) sel = i;
  h += F("<fieldset><legend>Meteostanice ČHMI</legend><label>Stanice</label><select id='st' name='st'>");
  for (int i = 0; i < NUM_STATIONS; i++) {
    h += F("<option value='");
    h += i;
    h += "'";
    if (i == sel) h += F(" selected");
    h += ">";
    h += STATIONS[i].name;
    h += F("</option>");
  }
  h += F("<option value='-1'");
  if (sel < 0) h += F(" selected");
  h += F(">jiná (zadat ručně)…</option></select><div id='custom'><label>WSI kód stanice</label>"
         "<input name='wsi' value='");
  h += esc(gCfg.wsi);
  h += F("'><label>Název</label><input name='name' value='");
  h += esc(gCfg.name);
  h += F("'><label>Nadmořská výška [m]</label><input name='elev' inputmode='numeric' value='");
  h += gCfg.elev;
  h += F("'><small>Seznam stanic: opendata.chmi.cz → meteorology/climate/now/metadata</small></div></fieldset>");

  // --- Firmware ---
  h += F("<fieldset><legend>Firmware</legend><p><small>Verze <b>" FW_VERSION "</b>");
  if (gOta.latest[0]) {
    h += F(", na GitHubu <b>");
    h += esc(gOta.latest);
    h += F("</b>");
  }
  static const char* const ST[] = {"", " – nekontrolováno", " – aktuální", " – je dostupná nová verze",
                                   " – stahuji…", " – chyba: "};
  if (gOta.state > OTA_CHECKING) {
    h += ST[gOta.state];
    if (gOta.state == OTA_FAILED) h += esc(gOta.msg);
  }
  h += F("</small></p><label style='display:flex;gap:8px;align-items:center'>"
         "<input type='checkbox' name='autoupd' value='1' style='width:auto'");
  if (gCfg.autoUpdate) h += F(" checked");
  h += F("> Automaticky instalovat nové verze</label>");
  if (!gAp)
    h += F("<button type='submit' formaction='/update' formnovalidate style='background:#1f6feb'>"
           "Zkontrolovat a nainstalovat nejnovější verzi</button>");
  h += F("</fieldset>");

  h += FPSTR(PAGE_TAIL);
  server.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  gLastActivity = millis();
  String ssid = server.arg("ssid");
  ssid.trim();
  if (!ssid.length()) {
    server.send(400, "text/plain; charset=utf-8", "Chybí název WiFi sítě.");
    return;
  }
  String pass = server.arg("pass");
  if (pass.length() || ssid != gCfg.ssid) gCfg.pass = pass;  // prázdné heslo u stejné sítě = beze změny
  gCfg.ssid = ssid;

  String la = server.arg("lat"), lo = server.arg("lon");
  la.replace(',', '.');
  lo.replace(',', '.');
  float lat = la.toFloat(), lon = lo.toFloat();
  if (lat != 0 && fabsf(lat) <= 90) gCfg.lat = lat;
  if (lon != 0 && fabsf(lon) <= 180) gCfg.lon = lon;

  int st = server.arg("st").toInt();
  if (server.arg("st") != "-1" && st >= 0 && st < NUM_STATIONS) {
    gCfg.wsi = STATIONS[st].wsi;
    gCfg.name = STATIONS[st].name;
    gCfg.elev = STATIONS[st].elev;
  } else {
    String w = server.arg("wsi");
    w.trim();
    if (w.length()) gCfg.wsi = w;
    String nm = server.arg("name");
    nm.trim();
    gCfg.name = nm.length() ? nm : gCfg.wsi;
    gCfg.elev = server.arg("elev").toInt();
  }
  gCfg.autoUpdate = server.hasArg("autoupd");
  settingsSave();
  Serial.printf("[Portal] uloženo: SSID=%s, %.5f %.5f, %s\n", gCfg.ssid.c_str(), gCfg.lat, gCfg.lon,
                gCfg.wsi.c_str());

  server.send(200, "text/html; charset=utf-8",
              F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                "<body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:24px'>"
                "<h2>Uloženo ✔</h2><p>Zařízení se restartuje a připojí k zadané WiFi. "
                "Tuto stránku můžeš zavřít.</p></body>"));
  delay(1500);
  ESP.restart();
}

static void handleUpdate() {
  gLastActivity = millis();
  gOtaRequest = true;
  server.send(200, "text/html; charset=utf-8",
              F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                "<meta http-equiv='refresh' content='20;url=/'>"
                "<body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:24px'>"
                "<h2>Kontroluji aktualizace…</h2><p>Průběh je vidět na displeji. Pokud je nová verze, "
                "zařízení ji stáhne a restartuje se (asi 1 min).</p></body>"));
}

static void handleNotFound() {
  gLastActivity = millis();
  if (gAp) {  // captive portál: vše přesměruj na nastavení
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Nenalezeno");
  }
}

void webBegin(bool apMode) {
  gAp = apMode;
  if (apMode) {
    WiFi.mode(WIFI_AP_STA);
    // ESP32-C3 Super Mini: s plným vysílacím výkonem hotspot často není vidět
    // (nebo deska padá na podpětí) -> snížit výkon hned po zapnutí WiFi i po startu AP
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    scan();  // před spuštěním AP, ať sken neruší klienty
    bool ok = WiFi.softAP(apName().c_str(), nullptr, 6);  // bez hesla, kanál 6
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    delay(100);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", IPAddress(192, 168, 4, 1));
    Serial.printf("[Portal] hotspot %s %s, IP %s, TX power %d\n", apName().c_str(), ok ? "OK" : "SELHAL",
                  WiFi.softAPIP().toString().c_str(), (int)WiFi.getTxPower());
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/system", HTTP_GET, handleSystem);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  gLastActivity = millis();
}

void webLoop() {
  if (gAp) dns.processNextRequest();
  server.handleClient();
}
