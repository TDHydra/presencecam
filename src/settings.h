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
  int8_t wifiTxPower;     // 0 = auto, else 0.25 dBm units (78 = 19.5 dBm ... 34 = 8.5 dBm)
  bool lowPowerLatched;   // auto mode found low power necessary (see power.h)
  String wifiSsid;
  String wifiPass;
  // Addressing, for Wi-Fi and Ethernet alike: DHCP, or a fixed address
  bool staticIp;
  String ip, mask, gateway, dns;
  bool debug;             // debug mode: everything on the console also goes to the debug log
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsForgetWifi();
String deviceId();  // last 4 hex digits of the MAC, e.g. "3F2A"
