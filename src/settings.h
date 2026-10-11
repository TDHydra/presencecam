// User settings, stored in NVS (survive reboots and firmware updates). The hardware
// (board, pins, sensors, display) is separate, see hwconfig.h.
#pragma once
#include <Arduino.h>

struct Settings {
  String name;          // shown on the OLED and web page, also the mDNS hostname
  uint16_t holdSeconds;
  uint16_t maxCm;
  uint16_t screenSleepS;  // turn the screen off after this long with nobody present (0 = never)
  bool screenFlip;        // rotate the screen 180 degrees
  String wifiSsid;
  String wifiPass;
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsForgetWifi();
String deviceId();  // last 4 hex digits of the MAC, e.g. "3F2A"
