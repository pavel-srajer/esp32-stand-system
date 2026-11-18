#include "portal.h"
#include "settings.h"
#include "ota.h"
#include "sysinfo.h"
#include "brightness.h"
#include "i18n.h"
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
  float lat, lon;  // přibližná poloha (pro výběr nejbližší stanice z mapy)
};
static const Station STATIONS[] = {
    {"0-20000-0-11518", "Praha-Ruzyně", 364, 50.101, 14.258},
    {"0-20000-0-11520", "Praha-Libuš", 302, 50.008, 14.447},
    {"0-20000-0-11406", "Cheb", 471, 50.068, 12.392},
    {"0-20000-0-11414", "Karlovy Vary", 603, 50.202, 12.914},
    {"0-20000-0-11433", "Kopisty", 240, 50.544, 13.623},
    {"0-20000-0-11464", "Milešovka", 833, 50.555, 13.931},
    {"0-20000-0-11487", "Kocelovice", 519, 49.467, 13.838},
    {"0-20000-0-11546", "České Budějovice", 388, 48.952, 14.469},
    {"0-20000-0-11636", "Churáňov", 1118, 49.068, 13.615},
    {"0-20000-0-11603", "Liberec", 398, 50.770, 15.024},
    {"0-20000-0-11723", "Brno-Tuřany", 241, 49.153, 16.689},
    {"0-20000-0-11698", "Kuchařovice", 334, 48.881, 16.086},
    {"0-20000-0-11782", "Ostrava-Mošnov", 251, 49.692, 18.112},
    {"0-20000-0-11787", "Lysá hora", 1322, 49.546, 18.448},
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

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="{{lang}}"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{t_set}}</title><style>
#map{height:300px;border-radius:8px;margin-top:8px}.srch{display:flex;gap:8px}.srch input{flex:1;min-width:0}
.sbtn{width:auto!important;margin:0!important;padding:10px 14px!important;font-size:1em!important;background:#1f6feb!important}
#near{display:block;margin-top:6px;color:#3fb950}
input[type=range]{padding:0;border:0;background:none;height:38px;accent-color:#58a6ff}.pct{color:#e6edf3;font-weight:600}
body{font-family:system-ui,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px}
main{max-width:460px;margin:auto}h1{font-size:1.4em;margin:.2em 0 .8em}
fieldset{border:1px solid #30363d;border-radius:10px;margin:0 0 16px;padding:12px 14px}
legend{color:#58a6ff;padding:0 6px}label{display:block;margin:10px 0 4px;font-size:.9em;color:#9da7b3}
input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #30363d;background:#161b22;color:#e6edf3;font-size:1em}
.row{display:flex;gap:10px}.row>div{flex:1}small{color:#7d8590}
button{width:100%;padding:14px;border:0;border-radius:10px;background:#238636;color:#fff;font-size:1.1em;margin-top:6px}
a{color:#58a6ff}.hide{display:none}</style></head><body><main>
<h1>{{h1}}</h1>
<p><a href="/system">{{sysinfo}} →</a></p><form method="post" action="/save">)HTML";

static const char PAGE_TAIL[] PROGMEM = R"HTML(<button type="submit">{{save}}</button></form>
<p><small>{{footer}}</small></p></main>
<script>
var s=document.getElementById('st'),c=document.getElementById('custom');
function u(){c.className=s.value=='-1'?'':'hide'}s.onchange=u;u();
var n=document.getElementById('ssid_sel'),m=document.getElementById('ssid');
n.onchange=function(){if(n.value)m.value=n.value;};
var bt=0;document.querySelectorAll('.br').forEach(function(r){r.oninput=function(){
 document.getElementById('v'+r.name).textContent=r.value;clearTimeout(bt);
 bt=setTimeout(function(){fetch('/api/bright?p='+r.value).catch(function(){})},150)}});
(function(){
var la=document.querySelector('[name=lat]'),lo=document.querySelector('[name=lon]'),near=document.getElementById('near');
function num(v){return parseFloat(String(v).replace(',','.'))}
function pick(lat,lon){var b=-1,bd=1e9;ST.forEach(function(p,i){var dy=(p[0]-lat)*111.2,dx=(p[1]-lon)*111.2*Math.cos(lat*Math.PI/180),d=Math.sqrt(dx*dx+dy*dy);if(d<bd){bd=d;b=i}});
 if(b>=0){s.value=b;u();near.textContent='{{nearest}}: '+s.options[s.selectedIndex].text+' ('+Math.round(bd)+' km)'}}
var css=document.createElement('link');css.rel='stylesheet';css.href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css';document.head.appendChild(css);
var js=document.createElement('script');js.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';
js.onload=function(){if(!window.L)return;
 document.getElementById('mapbox').className='';document.getElementById('nomap').className='hide';
 var p=[num(la.value)||50.08,num(lo.value)||14.43],map=L.map('map').setView(p,10);
 L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:18,attribution:'&copy; OpenStreetMap'}).addTo(map);
 var mk=L.marker(p,{draggable:true}).addTo(map),ci=L.circle(p,{radius:RK*1000,color:'#58a6ff',weight:1,fillOpacity:.06}).addTo(map);
 function set(ll,zoom){la.value=ll.lat.toFixed(5);lo.value=ll.lng.toFixed(5);mk.setLatLng(ll);ci.setLatLng(ll);if(zoom)map.setView(ll,11);pick(ll.lat,ll.lng)}
 map.on('click',function(e){set(e.latlng)});mk.on('dragend',function(){set(mk.getLatLng())});
 la.onchange=lo.onchange=function(){var a=num(la.value),b=num(lo.value);if(!isNaN(a)&&!isNaN(b)){var ll=L.latLng(a,b);mk.setLatLng(ll);ci.setLatLng(ll);map.panTo(ll);pick(a,b)}};
 var q=document.getElementById('q'),qb=document.getElementById('qb');
 qb.onclick=function(){if(!q.value)return;qb.textContent='…';
  fetch('https://nominatim.openstreetmap.org/search?format=json&limit=1&countrycodes=cz,sk,de,at,pl&q='+encodeURIComponent(q.value))
  .then(function(r){return r.json()}).then(function(j){qb.textContent='{{search}}';if(j[0])set(L.latLng(+j[0].lat,+j[0].lon),true);else near.textContent='{{notfound}}: '+q.value})
  .catch(function(){qb.textContent='{{search}}'})};
 q.onkeydown=function(e){if(e.key=='Enter'){e.preventDefault();qb.onclick()}};
 setTimeout(function(){map.invalidateSize()},200);
};
document.head.appendChild(js);
})();
</script></body></html>)HTML";

static const char PAGE_SYS[] PROGMEM = R"HTML(<!doctype html><html lang="{{lang}}"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{sysinfo}}</title><style>
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
<p><a href="/">← {{settings}}</a></p><h1>{{sysinfo}}</h1><div id="st">{{loading}}</div>
<div class="grid">
<div class="card"><div class="lbl">{{chiptemp}}</div><div class="big" id="temp">–</div><div class="bar"><i id="tempb"></i></div><div class="sub">{{sensor}}</div></div>
<div class="card"><div class="lbl">{{cpuload}}</div><div class="big" id="cpu">–</div><div class="bar"><i id="cpub"></i></div><div class="sub" id="cpus"></div></div>
<div class="card"><div class="lbl">RAM (heap)</div><div class="big" id="ram">–</div><div class="bar"><i id="ramb"></i></div><div class="sub" id="rams"></div></div>
<div class="card"><div class="lbl">Flash – firmware</div><div class="big" id="fl">–</div><div class="bar"><i id="flb"></i></div><div class="sub" id="fls"></div></div>
</div>
<h2>{{device}}</h2><table id="dev"></table>
<h2>WiFi</h2><table id="wifi"></table>
<h2>{{app}}</h2><table id="app"></table>
</main><script>
function kb(b){return b>=1048576?(b/1048576).toFixed(2)+' MB':(b/1024).toFixed(1)+' kB'}
function bar(id,p,w,c){var e=document.getElementById(id);e.style.width=Math.max(0,Math.min(100,p))+'%';e.className=p>=c?'c':p>=w?'w':''}
function up(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+' d ':'')+h+' h '+m+' min'}
function age(s){return s<0?'{{notyet}}':s<60?'{{ago_s}}'.replace('#',s):'{{ago_m}}'.replace('#',Math.round(s/60))}
function rows(id,r){document.getElementById(id).innerHTML=r.map(function(x){return '<tr><td>'+x[0]+'</td><td>'+x[1]+'</td></tr>'}).join('')}
function t(id,v){document.getElementById(id).textContent=v}
function esc(v){return String(v).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function load(){fetch('/api/status').then(function(r){return r.json()}).then(function(j){
 t('temp',j.temp==null?'–':String(j.temp).replace('.',',')+' °C');bar('tempb',(j.temp||0)/90*100,60,75);
 t('cpu',j.cpu<0?'–':j.cpu+' %');bar('cpub',j.cpu,60,85);t('cpus',j.mhz+' MHz, {{core}} (RISC-V)');
 var h=j.heap,used=h.total-h.free;t('ram',Math.round(used/h.total*100)+' %');bar('ramb',used/h.total*100,75,90);
 t('rams','{{free}} '+kb(h.free)+' / '+kb(h.total)+' · {{min}} '+kb(h.min)+' · {{block}} '+kb(h.block));
 var f=j.flash;t('fl',Math.round(f.used/f.slot*100)+' %');bar('flb',f.used/f.slot*100,80,92);
 t('fls',kb(f.used)+' / '+kb(f.slot)+' ({{part}} '+f.part+')');
 rows('dev',[['Firmware',esc(j.fw)],['{{built}}',esc(j.build)],['{{chip}}',esc(j.chip)+' rev '+j.rev+', '+j.mhz+' MHz'],
  ['{{flashtot}}',kb(f.size)+', '+f.mhz+' MHz'],['{{nvs}}',f.nvs_used+' / '+f.nvs_total],
  ['{{uptime}}',up(j.uptime)],['{{lastreset}}',esc(j.reset)]]);
 var w=j.wifi;rows('wifi',[['{{mode}}',esc(w.mode)+(w.connected?', {{connected}}':'')],['{{net}}',esc(w.ssid||'–')],
  ['{{signal}}',w.connected?w.rssi+' dBm':'–'],['{{channel}}',w.ch],['{{ip}}',esc(w.ip)],['MAC',esc(w.mac)]].concat(w.mode=='hotspot'?[['{{clients}}',w.clients]]:[]));
 var a=j.app;rows('app',[['{{planes}}',a.planes+' ({{range}} '+a.range+' km)'],['ADS-B',age(a.planes_age)+(a.planes_err?' · {{error}}: '+esc(a.planes_err):'')],
  ['{{station_w}}',age(a.weather_age)+(a.weather_err?' · {{error}}: '+esc(a.weather_err):'')],
  ['{{forecast}} Open-Meteo',age(a.fc_age)+(a.fc_age>=0?' · {{alerts}}: '+a.fc_alerts:'')+(a.fc_err?' · {{error}}: '+esc(a.fc_err):'')],
  ['{{rainradar}}',a.rain_frames+' {{frames}}, '+age(a.rain_age)+(a.rain_err?' · '+esc(a.rain_err):'')],['{{radarhere}}',esc(a.rain_info||'–')],['{{logos}}',esc(a.logo||'–')],['{{route}}',esc(a.route||'–')],['{{station}}',esc(a.station)],
  ['{{bright}}',a.bright+' % ('+esc(a.period)+'), '+esc(a.backlight)],['{{time}}',esc(a.ntp)],['{{update}}',esc(a.ota)],['{{stack}}',a.stack_net+' B (net), '+a.stack_loop+' B (UI)']]);
 t('st','{{updated}} '+new Date().toLocaleTimeString('{{locale}}'));
}).catch(function(){t('st','{{noresp}}')})}
load();setInterval(load,2000);
</script></body></html>)HTML";

// Překlady pro {{klíče}} v šablonách stránek (cs, en, de, it). V JS řetězcích bez apostrofů.
struct Tok {
  const char* k;
  const char* t[LANG_COUNT];
};
static const Tok TOKS[] = {
    {"lang", {"cs", "en", "de", "it"}},
    {"locale", {"cs-CZ", "en-GB", "de-DE", "it-IT"}},
    {"t_set", {"Nastavení – Letadla", "Settings – Planes", "Einstellungen – Flugzeuge", "Impostazioni – Aerei"}},
    {"h1", {"✈ Letadla &amp; počasí – nastavení", "✈ Planes &amp; weather – settings", "✈ Flugzeuge &amp; Wetter – Einstellungen", "✈ Aerei &amp; meteo – impostazioni"}},
    {"sysinfo", {"Systémové informace", "System information", "Systeminformationen", "Informazioni di sistema"}},
    {"settings", {"Nastavení", "Settings", "Einstellungen", "Impostazioni"}},
    {"save", {"Uložit a restartovat", "Save and restart", "Speichern und neu starten", "Salva e riavvia"}},
    {"footer", {"Nastavení se dá později otevřít i na IP adrese zařízení v domácí síti, nebo podržením tlačítka BOOT 5 s spustit tento hotspot znovu.",
                "Settings can later be opened at the device IP address in your home network, or hold the BOOT button for 5 s to start this hotspot again.",
                "Die Einstellungen sind später unter der IP-Adresse des Geräts im Heimnetz erreichbar, oder BOOT-Taste 5 s halten startet diesen Hotspot erneut.",
                "Le impostazioni si aprono poi all’indirizzo IP del dispositivo nella rete di casa, oppure tieni premuto BOOT per 5 s per riavviare questo hotspot."}},
    {"nearest", {"Nejbližší stanice ČHMI", "Nearest ČHMI station", "Nächste ČHMI-Station", "Stazione ČHMI più vicina"}},
    {"search", {"Hledat", "Search", "Suchen", "Cerca"}},
    {"notfound", {"Nenalezeno", "Not found", "Nicht gefunden", "Non trovato"}},
    {"loading", {"načítám…", "loading…", "lädt…", "caricamento…"}},
    {"chiptemp", {"Teplota čipu", "Chip temperature", "Chiptemperatur", "Temperatura chip"}},
    {"sensor", {"vnitřní senzor, ±2 °C", "internal sensor, ±2 °C", "interner Sensor, ±2 °C", "sensore interno, ±2 °C"}},
    {"cpuload", {"Vytížení CPU", "CPU load", "CPU-Last", "Carico CPU"}},
    {"device", {"Zařízení", "Device", "Gerät", "Dispositivo"}},
    {"app", {"Aplikace", "Application", "Anwendung", "Applicazione"}},
    {"notyet", {"zatím ne", "not yet", "noch nicht", "non ancora"}},
    {"ago_s", {"před # s", "# s ago", "vor # s", "# s fa"}},
    {"ago_m", {"před # min", "# min ago", "vor # min", "# min fa"}},
    {"core", {"1 jádro", "1 core", "1 Kern", "1 core"}},
    {"free", {"volno", "free", "frei", "libera"}},
    {"min", {"minimum", "minimum", "Minimum", "minimo"}},
    {"block", {"největší blok", "largest block", "größter Block", "blocco max"}},
    {"part", {"oddíl", "partition", "Partition", "partizione"}},
    {"built", {"Sestaveno", "Built", "Erstellt", "Compilato"}},
    {"chip", {"Čip", "Chip", "Chip", "Chip"}},
    {"flashtot", {"Flash celkem", "Total flash", "Flash gesamt", "Flash totale"}},
    {"nvs", {"Nastavení (NVS)", "Settings (NVS)", "Einstellungen (NVS)", "Impostazioni (NVS)"}},
    {"uptime", {"Běží", "Uptime", "Laufzeit", "Attivo da"}},
    {"lastreset", {"Poslední restart", "Last reset", "Letzter Neustart", "Ultimo riavvio"}},
    {"mode", {"Režim", "Mode", "Modus", "Modalità"}},
    {"connected", {"připojeno", "connected", "verbunden", "connesso"}},
    {"net", {"Síť", "Network", "Netzwerk", "Rete"}},
    {"signal", {"Signál", "Signal", "Signal", "Segnale"}},
    {"channel", {"Kanál", "Channel", "Kanal", "Canale"}},
    {"ip", {"IP adresa", "IP address", "IP-Adresse", "Indirizzo IP"}},
    {"clients", {"Připojení klienti", "Connected clients", "Verbundene Clients", "Client connessi"}},
    {"planes", {"Letadla v dosahu", "Aircraft in range", "Flugzeuge in Reichweite", "Aerei nel raggio"}},
    {"range", {"dosah", "range", "Reichweite", "raggio"}},
    {"error", {"chyba", "error", "Fehler", "errore"}},
    {"station_w", {"Počasí ČHMI (stanice)", "ČHMI weather (station)", "ČHMI-Wetter (Station)", "Meteo ČHMI (stazione)"}},
    {"forecast", {"Předpověď", "Forecast", "Vorhersage", "Previsioni"}},
    {"alerts", {"upozornění", "alerts", "Warnungen", "avvisi"}},
    {"rainradar", {"Meteoradar ČHMI", "ČHMI weather radar", "ČHMI-Wetterradar", "Radar meteo ČHMI"}},
    {"frames", {"snímků", "images", "Bilder", "immagini"}},
    {"radarhere", {"Radar u tebe", "Radar near you", "Radar bei dir", "Radar vicino a te"}},
    {"logos", {"Loga společností", "Airline logos", "Airline-Logos", "Loghi compagnie"}},
    {"route", {"Trasa letu", "Flight route", "Flugroute", "Rotta del volo"}},
    {"station", {"Stanice", "Station", "Station", "Stazione"}},
    {"bright", {"Jas displeje", "Display brightness", "Displayhelligkeit", "Luminosità display"}},
    {"time", {"Čas (NTP)", "Time (NTP)", "Zeit (NTP)", "Ora (NTP)"}},
    {"update", {"Aktualizace", "Update", "Update", "Aggiornamento"}},
    {"stack", {"Volný zásobník", "Free stack", "Freier Stack", "Stack libero"}},
    {"updated", {"aktualizováno", "updated", "aktualisiert", "aggiornato"}},
    {"noresp", {"zařízení neodpovídá", "device not responding", "Gerät antwortet nicht", "il dispositivo non risponde"}},
};

static String localize(const char* tpl) {
  int L = constrain(gCfg.lang, 0, LANG_COUNT - 1);
  String out;
  out.reserve(strlen(tpl) + 512);
  const char* p = tpl;
  for (;;) {
    const char* a = strstr(p, "{{");
    if (!a) {
      out += p;
      break;
    }
    out.concat(p, a - p);
    const char* b = strstr(a + 2, "}}");
    if (!b) {
      out += a;
      break;
    }
    bool found = false;
    for (const Tok& t : TOKS)
      if (strlen(t.k) == (size_t)(b - a - 2) && !strncmp(t.k, a + 2, b - a - 2)) {
        out += t.t[L];
        found = true;
        break;
      }
    if (!found) out.concat(a, b + 2 - a);
    p = b + 2;
  }
  return out;
}

static void handleSystem() {
  gLastActivity = millis();
  server.send(200, "text/html; charset=utf-8", localize(PAGE_SYS));
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

static void handleBright() {
  gLastActivity = millis();
  brightnessPreview(server.arg("p").toInt());
  server.send(204);
}

static void handleRoot() {
  gLastActivity = millis();
  if (server.hasArg("rescan")) scan();

  String h;
  h.reserve(6000);
  h += localize(PAGE_HEAD);

  // --- Jazyk ---
  {
    static const char* const LN[] = {"Čeština", "English", "Deutsch", "Italiano"};
    h += F("<fieldset><legend>Jazyk · Language · Sprache · Lingua</legend><select name='lang'>");
    for (int i = 0; i < LANG_COUNT; i++) {
      h += F("<option value='");
      h += i;
      h += "'";
      if (i == gCfg.lang) h += F(" selected");
      h += ">";
      h += LN[i];
      h += F("</option>");
    }
    h += F("</select></fieldset>");
  }

  // --- WiFi ---
  h += F("<fieldset><legend>WiFi</legend>");
  if (!gAp && WiFi.status() == WL_CONNECTED) {
    h += F("<p><small>");
    h += tr("Připojeno k", "Connected to", "Verbunden mit", "Connesso a");
    h += F(" <b>");
    h += esc(WiFi.SSID());
    h += F("</b>, IP ");
    h += WiFi.localIP().toString();
    h += F("</small></p>");
  }
  h += F("<label>");
  h += tr("Dostupné sítě", "Available networks", "Verfügbare Netze", "Reti disponibili");
  h += F(" (<a href='/?rescan=1'>");
  h += tr("vyhledat znovu", "rescan", "neu suchen", "ricerca");
  h += F("</a>)</label><select id='ssid_sel'><option value=''>— ");
  h += tr("vyber síť", "select network", "Netz wählen", "scegli rete");
  h += F(" —</option>");
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
    if (n.open) {
      h += ", ";
      h += tr("otevřená", "open", "offen", "aperta");
    }
    h += F(")</option>");
  }
  h += F("</select><label>");
  h += tr("Název sítě (SSID)", "Network name (SSID)", "Netzwerkname (SSID)", "Nome rete (SSID)");
  h += F("</label><input id='ssid' name='ssid' required value=\"");
  h += esc(gCfg.ssid);
  h += F("\"><label>");
  h += tr("Heslo", "Password", "Passwort", "Password");
  h += F("</label><input name='pass' type='password' autocomplete='off' placeholder='");
  h += gCfg.pass.length() ? tr("beze změny", "unchanged", "unverändert", "invariata")
                          : tr("prázdné = otevřená síť", "empty = open network", "leer = offenes Netz", "vuota = rete aperta");
  h += F("'></fieldset>");

  // --- Poloha ---
  h += F("<fieldset><legend>");
  h += tr("Poloha (střed radaru)", "Location (radar centre)", "Standort (Radarmitte)", "Posizione (centro radar)");
  h += F("</legend><div class='row'><div><label>");
  h += tr("Zeměpisná šířka", "Latitude", "Breitengrad", "Latitudine");
  h += F("</label><input name='lat' inputmode='decimal' required value='");
  h += String(gCfg.lat, 5);
  h += F("'></div><div><label>");
  h += tr("Zeměpisná délka", "Longitude", "Längengrad", "Longitudine");
  h += F("</label><input name='lon' inputmode='decimal' required value='");
  h += String(gCfg.lon, 5);
  h += F("'></div></div><div id='mapbox' class='hide'><label>");
  h += tr("Mapa – klikni na místo nebo přetáhni značku", "Map – click a place or drag the marker",
          "Karte – Ort anklicken oder Markierung ziehen", "Mappa – clicca un punto o trascina il segnaposto");
  h += F("</label><div class='srch'><input id='q' placeholder='");
  h += tr("Vyhledat obec nebo adresu", "Search town or address", "Ort oder Adresse suchen", "Cerca città o indirizzo");
  h += F("'><button type='button' id='qb' class='sbtn'>");
  h += tr("Hledat", "Search", "Suchen", "Cerca");
  h += F("</button></div><div id='map'></div></div><small id='nomap'>");
  h += tr("Mapa se zobrazí, když je telefon připojený k internetu (v domácí síti). Jinak: na mapy.cz pravé tlačítko na místo → souřadnice.",
          "The map appears when your phone is online (in your home network). Otherwise copy coordinates e.g. from Google Maps (right click).",
          "Die Karte erscheint, wenn das Handy online ist (im Heimnetz). Sonst Koordinaten z. B. aus Google Maps kopieren (Rechtsklick).",
          "La mappa appare quando il telefono è online (nella rete di casa). Altrimenti copia le coordinate ad es. da Google Maps (clic destro).");
  h += F("</small><small id='near'></small></fieldset>");

  // --- Obrazovky ---
  {
    const char* NAMES[] = {tr("Radar letadel", "Aircraft radar", "Flugzeugradar", "Radar aerei"),
                           tr("Detail letadla", "Aircraft detail", "Flugzeugdetail", "Dettaglio aereo"),
                           tr("Předpověď a upozornění", "Forecast and alerts", "Vorhersage und Warnungen", "Previsioni e avvisi"),
                           tr("Meteoradar ČHMI (animace)", "ČHMI weather radar (animated)", "ČHMI-Wetterradar (animiert)", "Radar meteo ČHMI (animato)"),
                           tr("Měření ze stanice ČHMI", "ČHMI station measurements", "ČHMI-Stationsmessung", "Misure stazione ČHMI"),
                           tr("Hodiny", "Clock", "Uhr", "Orologio")};
    h += F("<fieldset><legend>");
    h += tr("Obrazovky", "Screens", "Bildschirme", "Schermate");
    h += F("</legend><small>");
    h += tr("Krátký stisk tlačítka přepíná mezi zapnutými obrazovkami.", "A short button press switches between enabled screens.",
            "Kurzer Tastendruck wechselt zwischen aktiven Bildschirmen.", "Una breve pressione del tasto passa tra le schermate attive.");
    h += F("</small>");
    for (int i = 0; i < 6; i++) {
      h += F("<label style='display:flex;gap:8px;align-items:center'><input type='checkbox' style='width:auto' name='scr");
      h += i;
      h += "'";
      if (gCfg.screens & (1 << i)) h += F(" checked");
      h += "> ";
      h += NAMES[i];
      h += F("</label>");
    }
    static const int PR[] = {10, 25, 50, 100};
    h += F("<label>");
    h += tr("Dosah radaru letadel", "Aircraft radar range", "Reichweite Flugzeugradar", "Raggio radar aerei");
    h += F("</label><select name='prange'>");
    for (int i = 0; i < 4; i++) {
      h += F("<option value='");
      h += i;
      h += "'";
      if (i == gCfg.rangeIdx) h += F(" selected");
      h += ">";
      h += PR[i];
      h += F(" km</option>");
    }
    h += F("</select>");
    static const int RR[] = {25, 50, 100, 200};
    h += F("<label>");
    h += tr("Výřez meteoradaru (poloměr)", "Weather radar radius", "Wetterradar-Radius", "Raggio radar meteo");
    h += F("</label><select name='rrad'>");
    for (int i = 0; i < 4; i++) {
      h += F("<option value='");
      h += i;
      h += "'";
      if (i == gCfg.rainRadiusIdx) h += F(" selected");
      h += ">";
      h += RR[i];
      h += F(" km</option>");
    }
    h += F("</select><small>");
    h += tr("Oba dosahy jdou měnit i dlouhým stiskem tlačítka na příslušné obrazovce.", "Both ranges can also be changed by a long press on that screen.",
            "Beide Reichweiten lassen sich auch per langem Tastendruck ändern.", "Entrambi i raggi si cambiano anche con una pressione lunga.");
    h += F("</small>");
    const char* MAPS[] = {tr("žádný", "none", "keine", "nessuna"), tr("hranice a města", "borders and cities", "Grenzen und Städte", "confini e città"),
                          tr("hranice, řeky a města", "borders, rivers and cities", "Grenzen, Flüsse und Städte", "confini, fiumi e città")};
    auto mapSel = [&](const char* label, const char* name, int val) {
      h += F("<label>");
      h += label;
      h += F("</label><select name='");
      h += name;
      h += "'>";
      for (int i = 0; i < 3; i++) {
        h += F("<option value='");
        h += i;
        h += "'";
        if (i == val) h += F(" selected");
        h += ">";
        h += MAPS[i];
        h += F("</option>");
      }
      h += F("</select>");
    };
    {
      static const int CY[] = {0, 10, 15, 30, 60};
      h += F("<label>");
      h += tr("Automatické přepínání obrazovek", "Automatic screen rotation", "Automatischer Bildschirmwechsel", "Rotazione automatica schermate");
      h += F("</label><select name='cycle'>");
      for (int v : CY) {
        h += F("<option value='");
        h += v;
        h += "'";
        if (v == gCfg.cycleSec) h += F(" selected");
        h += ">";
        if (v) {
          h += tr("každých", "every", "alle", "ogni");
          h += String(" ") + v + " s";
        } else {
          h += tr("vypnuto", "off", "aus", "spento");
        }
        h += F("</option>");
      }
      h += F("</select><small>");
      h += tr("Po stisku tlačítka se automatika na minutu pozastaví.", "A button press pauses rotation for one minute.",
              "Ein Tastendruck pausiert den Wechsel für eine Minute.", "Premendo il tasto la rotazione si ferma per un minuto.");
      h += F("</small>");
    }
    mapSel(tr("Podklad meteoradaru", "Weather radar background", "Hintergrund Wetterradar", "Sfondo radar meteo"), "rmap", gCfg.rainMap);
    mapSel(tr("Podklad radaru letadel", "Aircraft radar background", "Hintergrund Flugzeugradar", "Sfondo radar aerei"), "pmap", gCfg.planeMap);
    h += F("<small>");
    h += tr("Obrysová mapa je uložená přímo v zařízení (hranice a řeky: Natural Earth). Hustota měst se řídí poloměrem.",
            "The outline map is stored on the device (borders and rivers: Natural Earth). City density follows the radius.",
            "Die Umrisskarte ist im Gerät gespeichert (Grenzen und Flüsse: Natural Earth). Die Städtedichte richtet sich nach dem Radius.",
            "La mappa è salvata nel dispositivo (confini e fiumi: Natural Earth). La densità delle città dipende dal raggio.");
    h += F("</small></fieldset>");
  }

  // --- Detail letadla ---
  {
    const char* DN[] = {tr("Logo společnosti", "Airline logo", "Airline-Logo", "Logo compagnia"),
                        tr("Trasa (PRG → LHR)", "Route (PRG → LHR)", "Route (PRG → LHR)", "Rotta (PRG → LHR)"),
                        tr("Typ a registrace", "Type and registration", "Typ und Kennzeichen", "Tipo e immatricolazione"),
                        tr("Výška v metrech (se šipkou stoupání/klesání)", "Altitude in metres (with climb/descent arrow)",
                           "Höhe in Metern (mit Steig-/Sinkpfeil)", "Quota in metri (con freccia salita/discesa)"),
                        tr("Výška ve stopách a letová hladina", "Altitude in feet and flight level", "Höhe in Fuß und Flugfläche", "Quota in piedi e livello di volo"),
                        tr("Rychlost a kurz", "Speed and track", "Geschwindigkeit und Kurs", "Velocità e rotta"),
                        tr("Vzdálenost a směr", "Distance and direction", "Entfernung und Richtung", "Distanza e direzione"),
                        tr("Pořadí letadla (3 / 17)", "Aircraft number (3 / 17)", "Flugzeugnummer (3 / 17)", "Numero aereo (3 / 17)")};
    h += F("<fieldset><legend>");
    h += tr("Detail letadla", "Aircraft detail", "Flugzeugdetail", "Dettaglio aereo");
    h += F("</legend><small>");
    h += tr("Volací znak se zobrazuje vždy. Zapnuté údaje se seřadí pod sebe a vystředí.",
            "The callsign is always shown. Enabled items are stacked and centred.",
            "Das Rufzeichen wird immer angezeigt. Aktive Angaben werden untereinander zentriert.",
            "Il nominativo è sempre mostrato. Le voci attive vengono impilate e centrate.");
    h += F("</small>");
    h += F("<label>");
    h += tr("Jednotky rychlosti", "Speed units", "Geschwindigkeitseinheit", "Unità di velocità");
    h += F("</label><select name='spdu'>");
    {
      const char* SU[] = {"km/h", tr("uzly (kt)", "knots (kt)", "Knoten (kt)", "nodi (kt)"), "mph"};
      for (int i = 0; i < 3; i++) {
        h += F("<option value='");
        h += i;
        h += "'";
        if (i == gCfg.speedUnit) h += F(" selected");
        h += ">";
        h += SU[i];
        h += F("</option>");
      }
    }
    h += F("</select>");
    for (int i = 0; i < 8; i++) {
      h += F("<label style='display:flex;gap:8px;align-items:center'><input type='checkbox' style='width:auto' name='det");
      h += i;
      h += "'";
      if (gCfg.detailMask & (1 << i)) h += F(" checked");
      h += "> ";
      h += DN[i];
      h += F("</label>");
    }
    h += F("</fieldset>");
  }

  // --- ČHMI ---
  int sel = -1;
  for (int i = 0; i < NUM_STATIONS; i++)
    if (gCfg.wsi == STATIONS[i].wsi) sel = i;
  h += F("<fieldset><legend>");
  h += tr("Meteostanice ČHMI (obrazovka Měření)", "ČHMI weather station (Measurements screen)", "ČHMI-Wetterstation (Bildschirm Messung)", "Stazione meteo ČHMI (schermata Misure)");
  h += F("</legend><label>");
  h += tr("Stanice", "Station", "Station", "Stazione");
  h += F("</label><select id='st' name='st'>");
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
  h += ">";
  h += tr("jiná (zadat ručně)…", "other (enter manually)…", "andere (manuell)…", "altra (manuale)…");
  h += F("</option></select><div id='custom'><label>");
  h += tr("WSI kód stanice", "Station WSI code", "WSI-Code der Station", "Codice WSI stazione");
  h += F("</label><input name='wsi' value='");
  h += esc(gCfg.wsi);
  h += F("'><label>");
  h += tr("Název", "Name", "Name", "Nome");
  h += F("</label><input name='name' value='");
  h += esc(gCfg.name);
  h += F("'><label>");
  h += tr("Nadmořská výška [m]", "Elevation [m]", "Höhe ü. M. [m]", "Altitudine [m]");
  h += F("</label><input name='elev' inputmode='numeric' value='");
  h += gCfg.elev;
  h += F("'><small>");
  h += tr("Seznam stanic", "Station list", "Stationsliste", "Elenco stazioni");
  h += F(": opendata.chmi.cz → meteorology/climate/now/metadata</small></div></fieldset>");

  // --- Jas ---
  h += F("<fieldset><legend>");
  h += tr("Jas displeje", "Display brightness", "Displayhelligkeit", "Luminosità display");
  h += F("</legend><label>");
  h += tr("Podsvícení (pin BL displeje)", "Backlight (display BL pin)", "Hintergrundbeleuchtung (BL-Pin)", "Retroilluminazione (pin BL)");
  h += F("</label><select name='bl'>");
  static const int BL_PINS[] = {-1, 5, 6, 7, 20, 21};
  for (int pin : BL_PINS) {
    h += F("<option value='");
    h += pin;
    h += "'";
    if (pin == gCfg.blPin) h += F(" selected");
    h += ">";
    if (pin < 0) h += tr("nepřipojeno – ztmavuje se obraz", "not connected – image is dimmed", "nicht verbunden – Bild wird abgedunkelt", "non collegato – si scurisce l’immagine");
    else h += String("GPIO ") + pin;
    h += F("</option>");
  }
  h += F("</select><small>");
  h += tr("Většina kulatých modulů nemá pin BL vyvedený a podsvícení svítí stále. Pak se ztmavuje jen obraz (černá dál slabě prosvítá). Má-li modul pin BL/BLK, připoj ho na volný GPIO a vyber ho zde.",
          "Most round modules have no BL pin and the backlight is always on, so only the image is dimmed (black still glows slightly). If your module has a BL/BLK pin, wire it to a free GPIO and select it here.",
          "Die meisten runden Module haben keinen BL-Pin; dann wird nur das Bild abgedunkelt (Schwarz leuchtet leicht). Hat das Modul einen BL/BLK-Pin, an einen freien GPIO anschließen und hier wählen.",
          "La maggior parte dei moduli rotondi non ha il pin BL: si scurisce solo l’immagine (il nero resta leggermente luminoso). Se il modulo ha il pin BL/BLK, collegalo a un GPIO libero e sceglilo qui.");
  h += F("</small>");
  struct Per {
    const char* label;
    const char* tn;
    const char* bn;
    int t, b;
  } per[3] = {{tr("Den", "Day", "Tag", "Giorno"), "tday", "bday", gCfg.tDay, gCfg.bDay},
              {tr("Večer", "Evening", "Abend", "Sera"), "teve", "beve", gCfg.tEve, gCfg.bEve},
              {tr("Noc", "Night", "Nacht", "Notte"), "tnight", "bnight", gCfg.tNight, gCfg.bNight}};
  for (auto& p : per) {
    char tm[8];
    snprintf(tm, sizeof tm, "%02d:%02d", p.t / 60, p.t % 60);
    h += F("<div class='row'><div><label>");
    h += p.label;
    h += " ";
    h += tr("od", "from", "ab", "dalle");
    h += F("</label><input type='time' name='");
    h += p.tn;
    h += F("' value='");
    h += tm;
    h += F("'></div><div><label>");
    h += tr("Jas", "Brightness", "Helligkeit", "Luminosità");
    h += F(" <span class='pct' id='v");
    h += p.bn;
    h += "'>";
    h += p.b;
    h += F("</span> %</label><input type='range' class='br' min='0' max='100' step='5' name='");
    h += p.bn;
    h += F("' value='");
    h += p.b;
    h += F("'></div></div>");
  }
  h += F("<small>");
  h += tr("Posunutím jezdce se jas na chvíli ukáže na displeji. Jas 0 % = zhasnuto. Je-li v noci displej ztlumený pod 30 %, první stisk tlačítka ho rozsvítí na 30 s.",
          "Moving a slider previews the brightness on the display. 0 % = off. If dimmed below 30 % at night, the first button press lights it up for 30 s.",
          "Beim Verschieben wird die Helligkeit kurz am Display gezeigt. 0 % = aus. Ist das Display nachts unter 30 %, schaltet der erste Tastendruck es für 30 s hell.",
          "Spostando il cursore la luminosità si vede sul display. 0 % = spento. Se di notte è sotto il 30 %, la prima pressione del tasto lo accende per 30 s.");
  h += F("</small></fieldset>");

  // --- Čas ---
  h += F("<fieldset><legend>");
  h += tr("Čas (NTP)", "Time (NTP)", "Zeit (NTP)", "Ora (NTP)");
  h += F("</legend><label>");
  h += tr("Hlavní NTP server", "Primary NTP server", "Primärer NTP-Server", "Server NTP principale");
  h += F("</label><input name='ntp1' value=\"");
  h += esc(gCfg.ntp1);
  h += F("\" placeholder='pool.ntp.org'><label>");
  h += tr("Záložní NTP server", "Backup NTP server", "Backup-NTP-Server", "Server NTP di riserva");
  h += F("</label><input name='ntp2' value=\"");
  h += esc(gCfg.ntp2);
  h += F("\" placeholder='–'><small>");
  h += tr("Např. pool.ntp.org, tik.cesnet.cz, ntp.nic.cz, time.google.com, nebo IP adresa routeru. Časová zóna je Praha (letní čas se přepíná sám).",
          "E.g. pool.ntp.org, tik.cesnet.cz, ntp.nic.cz, time.google.com or your router IP. Time zone is Central European (DST automatic).",
          "Z. B. pool.ntp.org, tik.cesnet.cz, ntp.nic.cz, time.google.com oder die Router-IP. Zeitzone MEZ (Sommerzeit automatisch).",
          "Es. pool.ntp.org, tik.cesnet.cz, ntp.nic.cz, time.google.com o l’IP del router. Fuso orario CET (ora legale automatica).");
  h += F("</small></fieldset>");

  // --- Firmware ---
  h += F("<fieldset><legend>Firmware</legend><p><small>");
  h += tr("Verze", "Version", "Version", "Versione");
  h += F(" <b>" FW_VERSION "</b>");
  if (gOta.latest[0]) {
    h += ", ";
    h += tr("na GitHubu", "on GitHub", "auf GitHub", "su GitHub");
    h += F(" <b>");
    h += esc(gOta.latest);
    h += F("</b>");
  }
  const char* ST[] = {"", tr(" – nekontrolováno", " – not checked", " – nicht geprüft", " – non verificato"),
                      tr(" – aktuální", " – up to date", " – aktuell", " – aggiornato"),
                      tr(" – je dostupná nová verze", " – new version available", " – neue Version verfügbar", " – nuova versione disponibile"),
                      tr(" – stahuji…", " – downloading…", " – lade herunter…", " – download…"),
                      tr(" – chyba: ", " – error: ", " – Fehler: ", " – errore: ")};
  if (gOta.state > OTA_CHECKING) {
    h += ST[gOta.state];
    if (gOta.state == OTA_FAILED) h += esc(gOta.msg);
  }
  h += F("</small></p><label style='display:flex;gap:8px;align-items:center'>"
         "<input type='checkbox' name='autoupd' value='1' style='width:auto'");
  if (gCfg.autoUpdate) h += F(" checked");
  h += "> ";
  h += tr("Automaticky instalovat nové verze", "Install new versions automatically", "Neue Versionen automatisch installieren", "Installa automaticamente le nuove versioni");
  h += F("</label>");
  if (!gAp) {
    h += F("<button type='submit' formaction='/update' formnovalidate style='background:#1f6feb'>");
    h += tr("Zkontrolovat a nainstalovat nejnovější verzi", "Check and install the latest version", "Neueste Version prüfen und installieren", "Verifica e installa l’ultima versione");
    h += F("</button>");
  }
  h += F("</fieldset>");

  // data pro mapu: polohy stanic a dosah radaru
  static const int RANGES[] = {10, 25, 50, 100};
  h += F("<script>var ST=[");
  for (int i = 0; i < NUM_STATIONS; i++) {
    if (i) h += ',';
    h += '[' + String(STATIONS[i].lat, 3) + ',' + String(STATIONS[i].lon, 3) + ']';
  }
  h += F("],RK=");
  h += RANGES[constrain(gCfg.rangeIdx, 0, 3)];
  h += F(";</script>");

  h += localize(PAGE_TAIL);
  server.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  gLastActivity = millis();
  String ssid = server.arg("ssid");
  ssid.trim();
  if (!ssid.length()) {
    server.send(400, "text/plain; charset=utf-8", tr("Chybí název WiFi sítě.", "WiFi network name is missing.", "WLAN-Name fehlt.", "Manca il nome della rete WiFi."));
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
  if (server.hasArg("lang")) gCfg.lang = constrain(server.arg("lang").toInt(), 0, LANG_COUNT - 1);
  if (server.hasArg("ntp1")) {
    String n1 = server.arg("ntp1"), n2 = server.arg("ntp2");
    n1.trim();
    n2.trim();
    gCfg.ntp1 = n1.length() ? n1 : String("pool.ntp.org");
    gCfg.ntp2 = n2;
  }
  {
    int m = 0;
    for (int i = 0; i < 6; i++)
      if (server.hasArg("scr" + String(i))) m |= 1 << i;
    gCfg.screens = m ? m : 1;
    int dm = 0;
    for (int i = 0; i < 8; i++)
      if (server.hasArg("det" + String(i))) dm |= 1 << i;
    gCfg.detailMask = dm;
    if (server.hasArg("spdu")) gCfg.speedUnit = constrain(server.arg("spdu").toInt(), 0, 2);
    if (server.hasArg("cycle")) gCfg.cycleSec = constrain(server.arg("cycle").toInt(), 0, 600);
    if (server.hasArg("rmap")) gCfg.rainMap = constrain(server.arg("rmap").toInt(), 0, 2);
    if (server.hasArg("pmap")) gCfg.planeMap = constrain(server.arg("pmap").toInt(), 0, 2);
    if (server.hasArg("prange")) gCfg.rangeIdx = constrain(server.arg("prange").toInt(), 0, 3);
    if (server.hasArg("rrad")) gCfg.rainRadiusIdx = constrain(server.arg("rrad").toInt(), 0, 3);
  }
  if (server.hasArg("bl")) gCfg.blPin = server.arg("bl").toInt();
  auto hm = [](const String& v, int def) {
    int hh, mm;
    return sscanf(v.c_str(), "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60 ? hh * 60 + mm : def;
  };
  auto pc = [](const String& v, int def) { return v.length() ? constrain(v.toInt(), 0, 100) : def; };
  gCfg.tDay = hm(server.arg("tday"), gCfg.tDay);
  gCfg.tEve = hm(server.arg("teve"), gCfg.tEve);
  gCfg.tNight = hm(server.arg("tnight"), gCfg.tNight);
  gCfg.bDay = pc(server.arg("bday"), gCfg.bDay);
  gCfg.bEve = pc(server.arg("beve"), gCfg.bEve);
  gCfg.bNight = pc(server.arg("bnight"), gCfg.bNight);
  settingsSave();
  Serial.printf("[Portal] uloženo: SSID=%s, %.5f %.5f, %s\n", gCfg.ssid.c_str(), gCfg.lat, gCfg.lon,
                gCfg.wsi.c_str());

  String pg = F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                "<body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:24px'><h2>");
  pg += tr("Uloženo ✔", "Saved ✔", "Gespeichert ✔", "Salvato ✔");
  pg += "</h2><p>";
  pg += tr("Zařízení se restartuje a připojí k zadané WiFi. Tuto stránku můžeš zavřít.",
           "The device restarts and connects to the selected WiFi. You can close this page.",
           "Das Gerät startet neu und verbindet sich mit dem gewählten WLAN. Du kannst diese Seite schließen.",
           "Il dispositivo si riavvia e si connette al WiFi scelto. Puoi chiudere questa pagina.");
  pg += "</p></body>";
  server.send(200, "text/html; charset=utf-8", pg);
  delay(1500);
  ESP.restart();
}

static void handleUpdate() {
  gLastActivity = millis();
  gOtaRequest = true;
  String pg = F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                "<meta http-equiv='refresh' content='20;url=/'>"
                "<body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:24px'><h2>");
  pg += tr("Kontroluji aktualizace…", "Checking for updates…", "Suche nach Updates…", "Verifica aggiornamenti…");
  pg += "</h2><p>";
  pg += tr("Průběh je vidět na displeji. Pokud je nová verze, zařízení se restartuje, stáhne ji a znovu restartuje (asi 1 min).",
           "Progress is shown on the display. If there is a new version, the device restarts, downloads it and restarts again (about 1 min).",
           "Der Fortschritt wird am Display angezeigt. Gibt es eine neue Version, startet das Gerät neu, lädt sie und startet erneut (ca. 1 min).",
           "L’avanzamento è sul display. Se c’è una nuova versione, il dispositivo si riavvia, la scarica e si riavvia di nuovo (circa 1 min).");
  pg += "</p></body>";
  server.send(200, "text/html; charset=utf-8", pg);
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
  server.on("/api/bright", HTTP_GET, handleBright);
  server.onNotFound(handleNotFound);
  server.begin();
  gLastActivity = millis();
}

void webLoop() {
  if (gAp) dns.processNextRequest();
  server.handleClient();
}
