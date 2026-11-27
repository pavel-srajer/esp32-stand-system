#pragma once
// Obrazovky – pro webové rozhraní (implementace v main.cpp)
int screenCount();
int screenCurrent();
bool screenEnabled(int i);
const char* screenName(int i);  // ve zvoleném jazyce
void screenSet(int i);          // přepne displej (a na minutu pozastaví automatické přepínání)
void noteSetAndShow(const String& text);  // uloží poznámku a ukáže ji na displeji

// Před uložením nastavení z webu: pozastaví displej (a síťovou úlohu, pokud web
// neběží v ní), aby nečetly nastavení během změn. Po uložení následuje restart.
void appPauseForRestart();
