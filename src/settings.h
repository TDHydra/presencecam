// User settings, stored in NVS (survive reboots and firmware updates).
#pragma once
#include <Arduino.h>

struct Settings {
  String name;          // shown on the OLED and web page, also the mDNS hostname
  uint16_t holdSeconds;
  uint16_t maxCm;
  uint16_t oledSleepS;  // turn the OLED off after this long with nobody present (0 = never)
  uint8_t oledDriver;   // 0 = SSD1306, 1 = SH1106
  bool oledFlip;
  String wifiSsid;
  String wifiPass;
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsForgetWifi();
String deviceId();  // last 4 hex digits of the MAC, e.g. "3F2A"
