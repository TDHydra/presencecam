// 128x64 I2C OLED. Short press on BOOT flips through the pages.
#pragma once
#include <Arduino.h>

void displayBegin();  // safe to call again after the driver setting changes
void displayLoop();
void displayNextPage();
void displayMessage(const String& line1, const String& line2 = "", uint32_t ms = 3000);
bool displayFound();
uint8_t displayAddress();  // 0x3C / 0x3D, 0 if none
