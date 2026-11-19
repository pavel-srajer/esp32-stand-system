#pragma once
// Obrazovky – pro webové rozhraní (implementace v main.cpp)
int screenCount();
int screenCurrent();
bool screenEnabled(int i);
const char* screenName(int i);  // ve zvoleném jazyce
void screenSet(int i);          // přepne displej (a na minutu pozastaví automatické přepínání)
