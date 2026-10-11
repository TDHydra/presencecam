// Serial console at 115200 baud. Type "help" and press Enter.
#pragma once
#include <Arduino.h>

void consoleByte(uint8_t b);
void consoleLoop();  // runs the streaming "radar" command
void printBanner(const char* resetReason);
