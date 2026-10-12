// Wi-Fi: joins the saved network, or opens a setup access point "PresenceCam-XXXX"
// with a captive portal. Also speaks Improv Serial, which lets the web flasher send
// the Wi-Fi details straight over USB right after flashing.
// Boards whose hardware config has an "eth" block (ESP32-P4-ETH, W5500 boards) use wired
// Ethernet with DHCP instead; the setup AP, join and scan calls then do nothing.
#pragma once
#include <Arduino.h>

#include <soc/soc_caps.h>

// Ethernet needs Arduino core 3 (the core 2 builds are Wi-Fi only)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
#define HAS_ETH 1
#else
#define HAS_ETH 0
#endif

// Wi-Fi on the chip, or through an ESP32-C6 co-processor (ESP-Hosted, ESP32-P4 boards)
#if SOC_WIFI_SUPPORTED || CONFIG_ESP_HOSTED_ENABLED
#define HAS_WIFI 1
#else
#define HAS_WIFI 0
#endif

void netBegin();
// Applies settings.staticIp/ip/mask/gateway/dns (or DHCP) to the active interface. Returns
// "" or what is wrong with the addresses. Called by netBegin and after the settings change.
String netApplyAddressing();
String netAddressing();  // "DHCP" or "static 192.168.1.50/24 via 192.168.1.1"
bool netWired();  // using Ethernet rather than Wi-Fi
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
