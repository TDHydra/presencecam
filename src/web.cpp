#include "web.h"

#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "net.h"
#include "presence.h"
#include "radar.h"
#include "settings.h"
#include "web_page.h"

static WebServer server(80);

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
  const RadarState& r = radar();
  uint32_t now = millis();
  String j;
  j.reserve(700);
  j += "{\"name\":\"" + jsonEscape(settings.name) + "\"";
  j += ",\"fw\":\"" FW_VERSION "\"";
  j += ",\"present\":" + String(p.present ? "true" : "false");
  j += ",\"stateFor\":" + String((now - p.sinceMs) / 1000);
  j += ",\"visitsToday\":" + String(p.visitsToday);
  j += ",\"radar\":{\"type\":\"" + String(radarType()) + "\",\"uart\":" + (r.uart ? "true" : "false");
  j += ",\"out\":" + String(r.outPin ? "true" : "false");
  j += ",\"detected\":" + String(r.detected ? "true" : "false");
  j += ",\"moving\":" + String(r.moving ? "true" : "false");
  j += ",\"still\":" + String(r.still ? "true" : "false");
  j += ",\"movingCm\":" + String(r.movingCm) + ",\"stillCm\":" + String(r.stillCm);
  j += ",\"distanceCm\":" + String(r.distanceCm);
  j += ",\"movingEnergy\":" + String(r.movingEnergy) + ",\"stillEnergy\":" + String(r.stillEnergy) + "}";
  j += ",\"settings\":{\"hold\":" + String(settings.holdSeconds) + ",\"maxcm\":" + String(settings.maxCm);
  j += ",\"oledsleep\":" + String(settings.oledSleepS) + ",\"oleddrv\":" + String(settings.oledDriver);
  j += ",\"oledflip\":" + String(settings.oledFlip ? "true" : "false") + "}";
  j += ",\"oled\":" + String(displayFound() ? "true" : "false");
  j += ",\"wifi\":{\"ssid\":\"" + jsonEscape(settings.wifiSsid) + "\",\"connected\":" + (netConnected() ? "true" : "false");
  j += ",\"ip\":\"" + (netConnected() ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"rssi\":" + String(netConnected() ? WiFi.RSSI() : 0);
  j += ",\"ap\":" + String(netApActive() ? "true" : "false") + ",\"host\":\"" + netHostname() + ".local\"}";
  j += ",\"time\":" + String((uint32_t)time(nullptr));
  j += ",\"uptime\":" + String(now / 1000);
  j += ",\"nextLine\":" + String(logNextLine());
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += "}";
  server.send(200, "application/json", j);
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
  if (server.hasArg("oledsleep")) settings.oledSleepS = constrain(server.arg("oledsleep").toInt(), 0, 86400);
  bool oledChanged = false;
  if (server.hasArg("oleddrv")) {
    uint8_t d = server.arg("oleddrv").toInt() ? 1 : 0;
    oledChanged |= d != settings.oledDriver;
    settings.oledDriver = d;
  }
  if (server.hasArg("oledflip")) {
    bool f = server.arg("oledflip") == "1" || server.arg("oledflip") == "true";
    oledChanged |= f != settings.oledFlip;
    settings.oledFlip = f;
  }
  settingsSave();
  if (oledChanged) displayBegin();
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
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/log", HTTP_GET, sendLogTail);
  server.on("/api/log.txt", HTTP_GET, sendLogFile);
  server.on("/api/verify", HTTP_GET, sendVerify);
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

void webLoop() { server.handleClient(); }
