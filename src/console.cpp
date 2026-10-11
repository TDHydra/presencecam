#include "console.h"

#include <Preferences.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "net.h"
#include "presence.h"
#include "hwconfig.h"
#include "sensors/sensor.h"
#include "settings.h"

static String line;
static uint32_t streamUntil = 0, streamNext = 0;

static const char* HELP =
    "Commands:\n"
    "  status            firmware, sensors, presence, Wi-Fi, display, log\n"
    "  sensors [s]       live sensor readings for s seconds (default 10)\n"
    "  config            show the hardware config (JSON) and its pins\n"
    "  config set {...}  replace the hardware config (the web flasher does this), then restart\n"
    "  config reset      back to this chip's built-in default, then restart\n"
    "  log [n]           last n log lines (default 10)\n"
    "  verify            check the log chain\n"
    "  join SSID PASS    join a network (use the web flasher or web page if the name has spaces)\n"
    "  forget            forget the saved Wi-Fi network\n"
    "  setup             open the setup network for 10 minutes\n"
    "  name NAME         rename the device\n"
    "  hold S            seconds to stay present after the last detection\n"
    "  maxcm CM          ignore LD2410 targets further away than this\n"
    "  flip              turn the screen upside down\n"
    "  reboot\n"
    "  factory YES       erase settings, Wi-Fi, hardware config and the log (new signing key)\n";

void printBanner(const char* resetReason) {
  Serial.printf("\nPresenceCam fw %s %s | %s | board %s | reset=%s | display %s | %d sensor(s)%s\n", FW_VERSION, FW_CHIP,
                settings.name.c_str(), hwBoardName(), resetReason, displayDescribe().c_str(), sensorsCount(),
                hwSafeMode ? " | SAFE MODE" : "");
  Serial.println("Type help + Enter for commands.");
}

static void printReading(Sensor& s, const Reading& r, void*) {
  if (!r.valid) Serial.printf(" %s=--", r.label);
  else if (r.boolean) Serial.printf(" %s=%s", r.label, r.value ? "yes" : "no");
  else Serial.printf(" %s=%.*f%s", r.label, r.decimals, r.value, r.unit);
}

static void status() {
  const Presence& p = presence();
  Serial.printf("  firmware  %s %s, %s, up %lus, heap %u KB free\n", FW_VERSION, FW_CHIP, settings.name.c_str(),
                millis() / 1000, ESP.getFreeHeap() / 1024);
  Serial.printf("  hardware  board %s, config %08lx%s\n", hwBoardName(), (unsigned long)hwHash(),
                hwSafeMode ? ", SAFE MODE" : "");
  if (sensorsGivePresence())
    Serial.printf("  presence  %s for %lus, %lu visit(s) today\n", p.present ? "PRESENT" : "clear",
                  (millis() - p.sinceMs) / 1000, (unsigned long)p.visitsToday);
  for (int i = 0; i < sensorsCount(); i++)
    Serial.printf("  sensor    %s: %s\n", sensorAt(i)->title(), sensorAt(i)->status().c_str());
  if (!sensorsCount()) Serial.println("  sensor    none configured");
  Serial.printf("  display   %s, layout %s\n", displayDescribe().c_str(), (const char*)(hw["layout"] | "basic"));
  Serial.printf("  settings  hold %us, max %ucm, screen sleep %us%s\n", settings.holdSeconds, settings.maxCm,
                settings.screenSleepS, settings.screenFlip ? ", flipped" : "");
  if (netConnected())
    Serial.printf("  wifi      %s, %s, %d dBm, http://%s.local\n", WiFi.SSID().c_str(), netUrl().c_str(),
                  WiFi.RSSI(), netHostname().c_str());
  else
    Serial.printf("  wifi      not connected%s%s\n", settings.wifiSsid.length() ? ", trying " : "",
                  settings.wifiSsid.c_str());
  if (netApActive()) Serial.printf("            setup AP \"%s\" at http://192.168.4.1\n", netApName().c_str());
  Serial.printf("  log       next line %lu\n", (unsigned long)logNextLine());
}

static void restartSoon() {
  Serial.flush();
  delay(300);
  ESP.restart();
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
  } else if (word == "sensors" || word == "radar") {
    uint32_t sec = arg.length() ? arg.toInt() : 10;
    streamUntil = millis() + sec * 1000;
    streamNext = 0;
  } else if (word == "config") {
    if (arg.startsWith("set")) {
      String err = hwSet(arg.substring(3));
      if (err.length()) {
        Serial.println("config error " + err);
      } else {
        Serial.printf("config ok %08lx\n", (unsigned long)hwHash());
        logEvent("hardware", String(hwBoardName()) + ", " + String(hw["sensors"].size()) + " sensor(s)");
        restartSoon();
      }
    } else if (arg == "reset") {
      hwReset();
      Serial.println("config ok default");
      restartSoon();
    } else {
      Serial.println("  " + hwJson());
      hwForEachPin([](const String& what, int gpio, void*) { Serial.printf("  GPIO%-3d %s\n", gpio, what.c_str()); },
                   nullptr);
    }
  } else if (word == "setup") {
    netStartSetupAp(10 * 60 * 1000);
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
  } else if (word == "flip") {
    settings.screenFlip = !settings.screenFlip;
    settingsSave();
    displayApplyFlip();
  } else if (word == "reboot" || word == "restart") {
    ESP.restart();
  } else if (word == "factory") {
    if (arg != "YES") {
      Serial.println("  this erases settings, Wi-Fi and the log. Type: factory YES");
      return;
    }
    logClearAll();
    hwReset();
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
  } else if (b >= 32 && line.length() < 1500) {
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
  streamNext = now + 250;
  Serial.printf("  %8lu", now);
  sensorsForEachReading(printReading, nullptr);
  if (sensorsGivePresence()) Serial.printf("  present=%s", presence().present ? "yes" : "no");
  Serial.println();
}
