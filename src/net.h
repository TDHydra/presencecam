// Wi-Fi: joins the saved network, or opens a setup access point "PresenceCam-XXXX"
// with a captive portal. Also speaks Improv Serial, which lets the web flasher send
// the Wi-Fi details straight over USB right after flashing.
// Chips without Wi-Fi (ESP32-P4) use wired Ethernet with DHCP instead; the setup AP, join
// and scan calls then do nothing.
#pragma once
#include <Arduino.h>

#if CONFIG_IDF_TARGET_ESP32P4
#define NET_WIRED 1
#else
#define NET_WIRED 0
#endif

void netBegin();
void netLoop();
bool netConnected();
bool netApActive();
void netApplyTxPower();  // after the Wi-Fi power setting changed
String netApName();
String netHostname();
String netUrl();  // http://<ip>/ when connected, "" otherwise
String netIp();   // "" when not connected
String netLink(); // the Wi-Fi network's name, or "Ethernet 100M"
int netRssi();    // dBm; 0 when not connected or wired
void netForget(); // drop the Wi-Fi connection (the caller clears the saved network)

// Scans for Wi-Fi networks and calls cb for each. Returns how many were found.
int netScan(void (*cb)(const String& ssid, int rssi, bool open, void* ctx), void* ctx);
void netStartSetupAp(uint32_t ms);  // open the setup network now; it closes after ms once Wi-Fi is up

// Saves the network and tries to join it. Returns true once connected (waits up to timeoutMs).
bool netJoin(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);

// Serial bytes go through here first; anything that isn't an Improv packet is handed to
// `console` one byte at a time.
void improvFeed(uint8_t b, void (*console)(uint8_t));
