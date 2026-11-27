#pragma once
#include <Arduino.h>

// Webové rozhraní pro nastavení.
//  apMode = true  -> otevřený hotspot "Letadla-XXXX" + captive portál (192.168.4.1)
//  apMode = false -> stejná stránka dostupná na IP adrese zařízení v domácí síti
void webBegin(bool apMode);
void webLoop();
// Web běží ve vlastní úloze (oddělené od displeje): pomalé posílání stránky po WiFi
// pak nezastaví displej ani tlačítko.
void webStartTask();
uint32_t webTaskStackFree();
String apName();
uint32_t webLastActivity();  // millis() posledního HTTP požadavku
