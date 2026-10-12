// Self-checks: what is wrong right now and what to do about it, for the web page, the screen
// and the console. Also: retries sensors that weren't found, and turns a crash loop into a
// safe-mode boot (no sensors, default config) so the board stays reachable.
#pragma once
#include <Arduino.h>

struct Problem {
  String what;  // "LD2410 radar not answering on UART (rx 16, tx 17)"
  String fix;   // "Check TX/RX are crossed and the module has 5 V"
};

void healthBegin();                         // after everything else in setup()
void healthLoop();
int healthProblems(Problem* out, int max);  // current problems, worst first; 0 = all fine
String healthSummary();                     // "OK" or "2 problems: ...; ..."
bool healthCrashLoop();                     // this boot is in safe mode because of repeated crashes
uint32_t healthCrashCount();                // crashes before this boot (0 after a clean run)
