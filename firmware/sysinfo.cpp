#include "sysinfo.h"
#include "ota.h"
#include "i18n.h"
#include <WiFi.h>
#include <esp_freertos_hooks.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// --- vytížení CPU ---
// Arduino SDK nemá zapnuté FreeRTOS run-time statistiky, proto vzorkujeme:
// v každém ticku (1 kHz) se podíváme, jestli zrovna běží idle task.
static TaskHandle_t sIdle = nullptr;
static volatile uint32_t sTicks = 0, sIdleTicks = 0;
static volatile int sLoad = -1;

static void IRAM_ATTR tickHook() {  // běží v přerušení (i během zápisu do flash -> IRAM)
  sTicks++;
  if (xTaskGetCurrentTaskHandle() == sIdle) sIdleTicks++;
  if (sTicks >= 1000) {
    sLoad = 100 - (int)(sIdleTicks * 100 / sTicks);
    sTicks = sIdleTicks = 0;
  }
}

void sysinfoBegin() {
  sIdle = xTaskGetIdleTaskHandleForCPU(0);
  esp_register_freertos_tick_hook_for_cpu(tickHook, 0);
}

int sysinfoCpuLoad() { return sLoad; }

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return tr("zapnutí napájení", "power on", "Einschalten", "accensione");
    case ESP_RST_SW: return tr("softwarový restart", "software restart", "Software-Neustart", "riavvio software");
    case ESP_RST_PANIC: return tr("pád (panic)", "crash (panic)", "Absturz (Panic)", "crash (panic)");
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return tr("podpětí (brownout)", "brownout", "Unterspannung", "sottotensione");
    case ESP_RST_DEEPSLEEP: return tr("probuzení", "wake-up", "Aufwachen", "risveglio");
    case ESP_RST_EXT: return tr("tlačítko RESET", "RESET button", "RESET-Taste", "tasto RESET");
    default: return tr("neznámý", "unknown", "unbekannt", "sconosciuto");
  }
}

void sysinfoJson(JsonObject o) {
  // teplotu čteme max. 1× za 2 s (senzor se pokaždé zapíná/vypíná)
  static float temp = NAN;
  static uint32_t tempAt = 0;
  if (isnan(temp) || millis() - tempAt > 2000) {
    temp = temperatureRead();
    tempAt = millis();
  }
  static uint32_t sketchSize = 0;
  if (!sketchSize) sketchSize = ESP.getSketchSize();

  o["fw"] = FW_VERSION;
  o["build"] = __DATE__ " " __TIME__;
  o["chip"] = ESP.getChipModel();
  o["rev"] = ESP.getChipRevision();
  o["mhz"] = ESP.getCpuFreqMHz();
  o["uptime"] = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  o["reset"] = resetReason();
  if (isnan(temp)) o["temp"] = nullptr;
  else o["temp"] = serialized(String(temp, 1));
  o["cpu"] = sLoad;

  JsonObject h = o["heap"].to<JsonObject>();
  h["total"] = ESP.getHeapSize();
  h["free"] = ESP.getFreeHeap();
  h["min"] = ESP.getMinFreeHeap();
  h["block"] = ESP.getMaxAllocHeap();

  JsonObject f = o["flash"].to<JsonObject>();
  f["size"] = ESP.getFlashChipSize();
  f["mhz"] = ESP.getFlashChipSpeed() / 1000000;
  const esp_partition_t* run = esp_ota_get_running_partition();
  f["part"] = run ? run->label : "?";
  f["slot"] = run ? run->size : 0;
  f["used"] = sketchSize;
  nvs_stats_t ns;
  if (nvs_get_stats(nullptr, &ns) == ESP_OK) {
    f["nvs_used"] = ns.used_entries;
    f["nvs_total"] = ns.total_entries;
  }

  JsonObject w = o["wifi"].to<JsonObject>();
  wifi_mode_t m = WiFi.getMode();
  w["mode"] = (m & WIFI_MODE_AP) ? "hotspot" : tr("klient", "client", "Client", "client");
  w["connected"] = WiFi.status() == WL_CONNECTED;
  w["ssid"] = WiFi.SSID();
  w["rssi"] = WiFi.RSSI();
  w["ch"] = WiFi.channel();
  w["ip"] = (m & WIFI_MODE_AP) && WiFi.status() != WL_CONNECTED ? WiFi.softAPIP().toString()
                                                                : WiFi.localIP().toString();
  w["mac"] = WiFi.macAddress();
  w["clients"] = WiFi.softAPgetStationNum();

  appStatusJson(o["app"].to<JsonObject>());
}
