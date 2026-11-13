#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Systémové informace pro webové rozhraní (/system, /api/status).
void sysinfoBegin();               // spustí měření vytížení CPU
void sysinfoJson(JsonObject o);    // naplní JSON
int sysinfoCpuLoad();              // vytížení CPU za poslední sekundu [%]

// Údaje aplikace (letadla, počasí, úlohy) – implementováno v main.cpp
void appStatusJson(JsonObject o);
