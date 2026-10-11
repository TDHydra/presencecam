#include "web.h"

#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "net.h"
#include "power.h"
#include "presence.h"
#include "hwconfig.h"
#include "sensors/sensor.h"
#include "settings.h"
#include "web_page.h"

static WebServer server(80);
static bool started = false;

static String jsonEscape(const String& s) {
  String o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 0x20) o += ' ';
    else o += ch;
  }
  return o;
}

static void sendStatus() {
  const Presence& p = presence();
  uint32_t now = millis();
  JsonDocument j;
  j["name"] = settings.name;
  j["fw"] = FW_VERSION;
  j["chip"] = FW_CHIP;
  j["board"] = hwBoardName();
  j["safeMode"] = hwSafeMode;
  j["presenceSensors"] = sensorsGivePresence();
  j["present"] = p.present;
  j["stateFor"] = (now - p.sinceMs) / 1000;
  j["visitsToday"] = p.visitsToday;

  JsonArray sensors = j["sensors"].to<JsonArray>();
  for (int i = 0; i < sensorsCount(); i++) {
    Sensor* s = sensorAt(i);
    JsonObject o = sensors.add<JsonObject>();
    o["type"] = s->type();
    o["title"] = s->title();
    o["ok"] = s->ok;
    o["status"] = s->status();
  }
  JsonArray readings = j["readings"].to<JsonArray>();
  sensorsForEachReading([](Sensor& s, const Reading& r, void* ctx) {
    JsonObject o = ((JsonArray*)ctx)->add<JsonObject>();
    o["sensor"] = s.type();
    o["label"] = r.label;
    o["unit"] = r.unit;
    o["valid"] = r.valid;
    if (r.boolean) o["value"] = r.value != 0;
    else o["value"] = r.value;
    if (r.max > 0) o["max"] = r.max;
  }, &readings);

  JsonObject st = j["settings"].to<JsonObject>();
  st["hold"] = settings.holdSeconds;
  st["maxcm"] = settings.maxCm;
  st["screensleep"] = settings.screenSleepS;
  st["screenflip"] = settings.screenFlip;
  j["display"] = displayDescribe();

  JsonObject w = j["wifi"].to<JsonObject>();
  w["ssid"] = settings.wifiSsid;
  w["connected"] = netConnected();
  w["ip"] = netConnected() ? WiFi.localIP().toString() : String("");
  w["rssi"] = netConnected() ? WiFi.RSSI() : 0;
  w["ap"] = netApActive();
  w["host"] = netHostname() + ".local";
  w["power"] = powerSummary();
  w["txpower"] = settings.wifiTxPower;
  w["brownouts"] = powerBrownouts();

  j["time"] = (uint32_t)time(nullptr);
  j["uptime"] = now / 1000;
  j["nextLine"] = logNextLine();
  j["heap"] = ESP.getFreeHeap();
  String out;
  serializeJson(j, out);
  server.send(200, "application/json", out);
}

// The active hardware config plus a flat pin list for the "Hardware" card.
static void sendHardware() {
  JsonDocument j;
  j["config"] = hw;
  j["safeMode"] = hwSafeMode;
  j["hash"] = String(hwHash(), HEX);
  j["display"] = displayDescribe();
  JsonArray pins = j["pins"].to<JsonArray>();
  hwForEachPin([](const String& what, int gpio, void* ctx) {
    JsonObject o = ((JsonArray*)ctx)->add<JsonObject>();
    o["what"] = what;
    o["gpio"] = gpio;
  }, &pins);
  String out;
  serializeJson(j, out);
  server.send(200, "application/json", out);
}

static void sendLogTail() {
  size_t n = server.hasArg("n") ? constrain(server.arg("n").toInt(), 1, 500) : 50;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  logTail(n, [](const String& line, void*) { server.sendContent(line + "\n"); }, nullptr);
  server.sendContent("");
}

static void sendLogFile() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + netHostname() + "-log.txt\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  logForEach([](const String& line, void*) { server.sendContent(line + "\n"); }, nullptr);
  server.sendContent("");
}

static void sendVerify() {
  VerifyResult v = logVerify();
  String j = "{\"ok\":" + String(v.issues ? "false" : "true");
  j += ",\"lines\":" + String(v.lines) + ",\"first\":" + String(v.first) + ",\"last\":" + String(v.last);
  j += ",\"startsMidChain\":" + String(v.startsMidChain ? "true" : "false");
  j += ",\"issues\":" + String(v.issues) + ",\"report\":\"" + jsonEscape(v.report) + "\"}";
  server.send(200, "application/json", j);
}

static void postSettings() {
  String changed;
  if (server.hasArg("name") && server.arg("name").length()) {
    settings.name = server.arg("name").substring(0, 32);
    changed += " name";
  }
  if (server.hasArg("hold")) settings.holdSeconds = constrain(server.arg("hold").toInt(), 1, 3600);
  if (server.hasArg("maxcm")) settings.maxCm = constrain(server.arg("maxcm").toInt(), 30, 800);
  if (server.hasArg("screensleep")) settings.screenSleepS = constrain(server.arg("screensleep").toInt(), 0, 86400);
  if (server.hasArg("txpower")) {
    settings.wifiTxPower = constrain(server.arg("txpower").toInt(), 0, 78);
    if (!settings.wifiTxPower) settings.lowPowerLatched = false;  // "auto" starts over at full power
    netApplyTxPower();
  }
  if (server.hasArg("screenflip")) {
    settings.screenFlip = server.arg("screenflip") == "1" || server.arg("screenflip") == "true";
    displayApplyFlip();
  }
  settingsSave();
  logEvent("settings", "hold " + String(settings.holdSeconds) + " s, max " + String(settings.maxCm) + " cm" + changed);
  sendStatus();
}

static void postWifi() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (!ssid.length()) { server.send(400, "text/plain", "ssid missing"); return; }
  // Answer first: if we're on the setup AP, the phone may lose the page while we switch.
  server.send(200, "text/plain", "joining");
  delay(200);
  netJoin(ssid, pass, 15000);
}

static void sendScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

static void updateDone() {
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "Update OK, restarting" : String("Update failed: ") + Update.errorString());
  if (ok) {
    logEvent("update", "firmware uploaded from the web page");
    displayMessage("Updated", "restarting...", 5000);
    delay(500);
    ESP.restart();
  }
}

static void updateChunk() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    Serial.printf("update: receiving %s\n", u.filename.c_str());
    displayMessage("Updating...", u.filename, 60000);
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    Update.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("update: %u bytes, %s\n", u.totalSize, Update.hasError() ? Update.errorString() : "ok");
  }
}

void webBegin() {
  if (!powerWifiAllowed()) return;  // no network stack without Wi-Fi; starting the server would crash
  started = true;
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/log", HTTP_GET, sendLogTail);
  server.on("/api/log.txt", HTTP_GET, sendLogFile);
  server.on("/api/verify", HTTP_GET, sendVerify);
  server.on("/api/hardware", HTTP_GET, sendHardware);
  server.on("/api/settings", HTTP_GET, sendStatus);
  server.on("/api/settings", HTTP_POST, postSettings);
  server.on("/api/wifi", HTTP_POST, postWifi);
  server.on("/api/scan", HTTP_GET, sendScan);
  server.on("/api/reboot", HTTP_POST, [] {
    server.send(200, "text/plain", "restarting");
    delay(300);
    ESP.restart();
  });
  server.on("/update", HTTP_POST, updateDone, updateChunk);
  // Captive portal: phones probe random URLs on the setup AP; send them all to the page.
  server.onNotFound([] {
    if (netApActive()) {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  server.begin();
}

void webLoop() {
  if (started) server.handleClient();
}
