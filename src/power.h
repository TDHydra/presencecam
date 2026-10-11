// Brownout recovery. When USB power is too weak, the Wi-Fi radio's start-up current spike
// trips the brownout detector and the board resets, over and over. This counts brownout
// resets in RTC memory (no flash writes, which matters in a reset loop) and steps down:
//   no brownouts      Wi-Fi at the configured power (auto = full, or low if it was needed before)
//   1 brownout        Wi-Fi at 8.5 dBm
//   2 or more         Wi-Fi off for this boot, with a message on screen; screen, sensors and
//                     console keep working. A power cycle or RST tries again.
// After 60 s without trouble the count clears, and if low power was needed it is remembered
// (auto mode only), so later starts go straight to low power.
#pragma once
#include <Arduino.h>

void powerBegin();            // first thing in setup()
void powerLoop();
uint32_t powerBrownouts();    // brownout resets in a row before this boot
bool powerWifiAllowed();
int8_t powerTxQuarterDbm();   // Wi-Fi TX power to use, in 0.25 dBm units (ESP-IDF's scale)
String powerSummary();        // one line for the console / web page
