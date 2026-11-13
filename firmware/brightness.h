#pragma once
// Jas displeje (implementace v main.cpp)
void brightnessPreview(int pct);   // dočasně (4 s) nastaví jas – náhled z webu
int brightnessCurrent();           // aktuální jas [%]
const char* brightnessPeriod();    // "den" / "večer" / "noc"
