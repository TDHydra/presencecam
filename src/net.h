// Wi-Fi: joins the saved network, or opens a setup access point "PresenceCam-XXXX"
// with a captive portal. Also speaks Improv Serial, which lets the web flasher send
// the Wi-Fi details straight over USB right after flashing.
#pragma once
#include <Arduino.h>

void netBegin();
void netLoop();
bool netConnected();
bool netApActive();
void netApplyTxPower();  // after the Wi-Fi power setting changed
String netApName();
String netHostname();
String netUrl();  // http://<ip>/ when connected, "" otherwise
void netStartSetupAp(uint32_t ms);  // open the setup network now; it closes after ms once Wi-Fi is up

// Saves the network and tries to join it. Returns true once connected (waits up to timeoutMs).
bool netJoin(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);

// Serial bytes go through here first; anything that isn't an Improv packet is handed to
// `console` one byte at a time.
void improvFeed(uint8_t b, void (*console)(uint8_t));
