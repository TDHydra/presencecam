#include "console.h"

#include <Preferences.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "net.h"
#include "presence.h"
#include "radar.h"
#include "settings.h"

static String line;
static uint32_t streamUntil = 0, streamNext = 0;

static const char* HELP =
    "Commands:\n"
    "  status            firmware, radar, presence, Wi-Fi, OLED, log\n"
    "  radar [s]         live radar readings for s seconds (default 10)\n"
    "  log [n]           last n log lines (default 10)\n"
    "  verify            check the log chain\n"
    "  wifi              Wi-Fi state\n"
    "  join SSID PASS    join a network (use the web flasher or web page if the name has spaces)\n"
    "  forget            forget the saved Wi-Fi network\n"
    "  name NAME         rename the device\n"
    "  hold S            seconds to stay present after the last detection\n"
    "  maxcm CM          ignore LD2410 targets further away than this\n"
    "  oled ssd1306|sh1106|flip   pick the OLED driver or turn the picture upside down\n"
    "  reboot\n"
    "  factory YES       erase settings, Wi-Fi and the log (new signing key)\n";

void printBanner(const char* resetReason) {
  Serial.printf("\nPresenceCam fw %s | %s | reset=%s | radar OUT=%d | oled=%s\n", FW_VERSION,
                settings.name.c_str(), resetReason, digitalRead(PIN_RADAR_OUT),
                displayFound() ? "yes" : "no");
  Serial.println("Type help + Enter for commands.");
}

static void status() {
  const Presence& p = presence();
  const RadarState& r = radar();
  Serial.printf("  firmware  %s, %s, up %lus, heap %u KB free\n", FW_VERSION, settings.name.c_str(),
                millis() / 1000, ESP.getFreeHeap() / 1024);
  Serial.printf("  presence  %s for %lus, %lu visit(s) today\n", p.present ? "PRESENT" : "clear",
                (millis() - p.sinceMs) / 1000, (unsigned long)p.visitsToday);
  Serial.printf("  radar     %s, OUT=%d, frames=%lu\n", radarType(), r.outPin, (unsigned long)r.frames);
  if (r.uart)
    Serial.printf("            moving %d (%u cm, energy %u)  still %d (%u cm, energy %u)  distance %u cm\n",
                  r.moving, r.movingCm, r.movingEnergy, r.still, r.stillCm, r.stillEnergy, r.distanceCm);
  Serial.printf("  settings  hold %us, max %ucm, oled %s%s, oled sleep %us\n", settings.holdSeconds,
                settings.maxCm, settings.oledDriver ? "SH1106" : "SSD1306", settings.oledFlip ? " flipped" : "",
                settings.oledSleepS);
  Serial.printf("  oled      %s\n", displayFound() ? String("found at 0x" + String(displayAddress(), HEX)).c_str()
                                                   : "not found (check SDA=21, SCL=22, 3V3, GND)");
  if (netConnected())
    Serial.printf("  wifi      %s, %s, %d dBm, http://%s.local\n", WiFi.SSID().c_str(), netUrl().c_str(),
                  WiFi.RSSI(), netHostname().c_str());
  else
    Serial.printf("  wifi      not connected%s%s\n", settings.wifiSsid.length() ? ", trying " : "",
                  settings.wifiSsid.c_str());
  if (netApActive()) Serial.printf("            setup AP \"%s\" at http://192.168.4.1\n", netApName().c_str());
  Serial.printf("  log       next line %lu\n", (unsigned long)logNextLine());
}

static void run(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;
  int sp = cmd.indexOf(' ');
  String word = sp < 0 ? cmd : cmd.substring(0, sp);
  String arg = sp < 0 ? "" : cmd.substring(sp + 1);
  arg.trim();
  word.toLowerCase();

  if (word == "help" || word == "?") {
    Serial.print(HELP);
  } else if (word == "status") {
    status();
  } else if (word == "radar") {
    uint32_t s = arg.length() ? arg.toInt() : 10;
    streamUntil = millis() + s * 1000;
    streamNext = 0;
    Serial.println("  t(ms)    OUT  det  mov(cm/E)   still(cm/E)  dist(cm)  present");
  } else if (word == "log") {
    size_t n = arg.length() ? arg.toInt() : 10;
    logTail(n, [](const String& l, void*) { Serial.println("  " + l); }, nullptr);
  } else if (word == "verify") {
    VerifyResult v = logVerify();
    Serial.printf("  %lu line(s) checked (%lu-%lu)%s: %s\n", (unsigned long)v.lines, (unsigned long)v.first,
                  (unsigned long)v.last, v.startsMidChain ? ", older lines rotated out" : "",
                  v.issues ? "PROBLEMS" : "OK");
    if (v.report.length()) Serial.print(v.report);
  } else if (word == "wifi") {
    status();
  } else if (word == "join") {
    int s2 = arg.indexOf(' ');
    String ssid = s2 < 0 ? arg : arg.substring(0, s2);
    String pass = s2 < 0 ? "" : arg.substring(s2 + 1);
    Serial.println(netJoin(ssid, pass) ? "  connected: " + netUrl() : String("  could not connect (still trying)"));
  } else if (word == "forget") {
    settingsForgetWifi();
    WiFi.disconnect(true);
    Serial.println("  Wi-Fi forgotten; restarting into setup mode");
    delay(300);
    ESP.restart();
  } else if (word == "name" && arg.length()) {
    settings.name = arg.substring(0, 32);
    settingsSave();
    Serial.println("  renamed; restart to update the network hostname");
  } else if (word == "hold" && arg.toInt() > 0) {
    settings.holdSeconds = constrain(arg.toInt(), 1, 3600);
    settingsSave();
    Serial.printf("  hold %us\n", settings.holdSeconds);
  } else if (word == "maxcm" && arg.toInt() > 0) {
    settings.maxCm = constrain(arg.toInt(), 30, 800);
    settingsSave();
    Serial.printf("  max %ucm\n", settings.maxCm);
  } else if (word == "oled") {
    if (arg == "sh1106") settings.oledDriver = 1;
    else if (arg == "ssd1306") settings.oledDriver = 0;
    else if (arg == "flip") settings.oledFlip = !settings.oledFlip;
    settingsSave();
    displayBegin();
  } else if (word == "reboot" || word == "restart") {
    ESP.restart();
  } else if (word == "factory") {
    if (arg != "YES") {
      Serial.println("  this erases settings, Wi-Fi and the log. Type: factory YES");
      return;
    }
    logClearAll();
    settingsForgetWifi();
    Preferences p;
    p.begin("pcam", false);
    p.clear();
    p.end();
    Serial.println("  erased, restarting");
    delay(300);
    ESP.restart();
  } else {
    Serial.println("  unknown command, type help");
  }
}

void consoleByte(uint8_t b) {
  if (b == '\r' || b == '\n') {
    if (line.length()) {
      Serial.println("> " + line);
      run(line);
    }
    line = "";
  } else if (b == 8 || b == 127) {
    if (line.length()) line.remove(line.length() - 1);
  } else if (b >= 32 && line.length() < 120) {
    line += (char)b;
  }
}

void consoleLoop() {
  if (!streamUntil) return;
  uint32_t now = millis();
  if ((int32_t)(now - streamUntil) > 0) {
    streamUntil = 0;
    return;
  }
  if (now < streamNext) return;
  streamNext = now + 200;
  const RadarState& r = radar();
  Serial.printf("  %-8lu %-4d %-4d %d %4u/%-3u  %d %4u/%-3u  %-8u  %s\n", now, r.outPin, r.detected, r.moving,
                r.movingCm, r.movingEnergy, r.still, r.stillCm, r.stillEnergy, r.distanceCm,
                presence().present ? "yes" : "no");
}
