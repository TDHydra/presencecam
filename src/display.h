// The screen: picks the backend from the hardware config, runs the chosen layout, and adds
// the shared pages (network, device), messages, the info screen and button hints.
#pragma once
#include <Arduino.h>

void displayBegin();
void displayLoop();
bool displayFound();
String displayDescribe();  // "SSD1306 128x64 at 0x3C", "none", "not found"

void displayNextPage();
void displayToggleDim();
void displayWake();         // turn the screen back on if it went to sleep
void displayApplyFlip();    // after settings.screenFlip changed
void displayMessage(const String& line1, const String& line2 = "", uint32_t ms = 3000);
void displayShowInfo(uint32_t ms);  // address + QR code for the web page
// While a button is held: what it does if released now, and what comes next at nextAtMs.
void displayHoldHint(uint32_t heldMs, const char* nowAction, const char* nextAction, uint32_t nextAtMs);
void displayHoldHintClear();
