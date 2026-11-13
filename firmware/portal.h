#pragma once
#include <Arduino.h>

// Webové rozhraní pro nastavení.
//  apMode = true  -> otevřený hotspot "Letadla-XXXX" + captive portál (192.168.4.1)
//  apMode = false -> stejná stránka dostupná na IP adrese zařízení v domácí síti
void webBegin(bool apMode);
void webLoop();
String apName();
uint32_t webLastActivity();  // millis() posledního HTTP požadavku
