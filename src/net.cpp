#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "power.h"
#include "settings.h"

static DNSServer dns;
static bool apOn = false;
static bool wasConnected = false;
static uint32_t lostSince = 0;
static uint32_t apOffAt = 0;  // close the setup AP a while after we got online
static bool radioOff = false;  // Wi-Fi disabled for this boot because power is too weak

static void applyTxPower() {
  WiFi.setTxPower((wifi_power_t)powerTxQuarterDbm());
}

String netApName() { return "PresenceCam-" + deviceId(); }

String netHostname() {
  String h;
  for (char ch : settings.name) {
    if (isalnum((unsigned char)ch)) h += (char)tolower(ch);
    else if (h.length() && h[h.length() - 1] != '-') h += '-';
  }
  while (h.endsWith("-")) h.remove(h.length() - 1);
  return h.length() ? h : "presencecam";
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
bool netApActive() { return apOn; }
String netUrl() { return netConnected() ? "http://" + WiFi.localIP().toString() + "/" : ""; }

static void startAp() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(netApName().c_str());
  applyTxPower();
  dns.start(53, "*", WiFi.softAPIP());  // every name resolves to us -> captive portal
  apOn = true;
  Serial.printf("wifi: setup access point \"%s\" at http://192.168.4.1\n", netApName().c_str());
  displayMessage("Wi-Fi setup", netApName(), 5000);
}

static void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  Serial.println("wifi: setup access point closed");
}

void netBegin() {
  WiFi.persistent(false);  // we keep credentials in our own NVS namespace
  if (!powerWifiAllowed()) {
    radioOff = true;
    WiFi.mode(WIFI_OFF);
    Serial.println("wifi: OFF for this boot, the USB power browned out twice when Wi-Fi started.");
    Serial.println("      Use a better cable or charger, or add a 470-1000 uF capacitor across 3V3 and GND,");
    Serial.println("      then power-cycle or press RST.");
    displayMessage("Power too weak", "Wi-Fi off. See README", 20000);
    return;
  }
  if (powerTxQuarterDbm() < 78) {
    // Low-power start: lower CPU clock and give the supply a moment before the radio spike
    setCpuFrequencyMhz(80);
    delay(500);
  }
  WiFi.setHostname(netHostname().c_str());
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setAutoReconnect(true);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  if (settings.wifiSsid.length()) {
    Serial.printf("wifi: joining \"%s\" at %.1f dBm\n", settings.wifiSsid.c_str(), powerTxQuarterDbm() / 4.0f);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    applyTxPower();
    lostSince = millis();
  } else {
    startAp();
  }
}

void netLoop() {
  if (radioOff) return;
  if (apOn) dns.processNextRequest();
  uint32_t now = millis();
  bool up = netConnected();

  if (up && !wasConnected) {
    wasConnected = true;
    Serial.printf("wifi: connected, %s  (http://%s.local)\n", netUrl().c_str(), netHostname().c_str());
    MDNS.end();
    if (MDNS.begin(netHostname().c_str())) MDNS.addService("http", "tcp", 80);
    displayMessage("Wi-Fi connected", WiFi.localIP().toString(), 5000);
    logEvent("wifi", WiFi.localIP().toString());
    if (apOn) apOffAt = max(apOffAt, now + 60000);  // give the setup page time to show the new address
  } else if (!up && wasConnected) {
    wasConnected = false;
    lostSince = now;
    Serial.println("wifi: connection lost, retrying");
  }

  if (apOffAt && now > apOffAt) {
    apOffAt = 0;
    if (up) stopAp();
  }
  // Can't reach the saved network for 30 s: open the setup AP so it can be changed.
  // Auto-reconnect keeps trying in the background, and the AP closes again once we're back.
  if (!up && !apOn && now - lostSince > 30000) startAp();
}

void netApplyTxPower() {
  if (!radioOff) applyTxPower();
}

void netStartSetupAp(uint32_t ms) {
  if (radioOff) return;
  startAp();
  apOffAt = millis() + ms;
}

bool netJoin(const String& ssid, const String& pass, uint32_t timeoutMs) {
  settings.wifiSsid = ssid;
  settings.wifiPass = pass;
  settingsSave();
  Serial.printf("wifi: joining \"%s\"\n", ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  applyTxPower();
  lostSince = millis();
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (netConnected()) return true;
    delay(100);
  }
  return netConnected();
}

// ---------------------------------------------------------------------------------------
// Improv Serial (https://www.improv-wifi.com/serial/), used by ESP Web Tools.
// Packet: "IMPROV" | version 1 | type | length | data... | checksum (sum of all bytes)

enum : uint8_t { IMP_STATE = 1, IMP_ERROR = 2, IMP_RPC = 3, IMP_RESULT = 4 };
enum : uint8_t { ST_READY = 2, ST_PROVISIONING = 3, ST_PROVISIONED = 4 };
enum : uint8_t { CMD_WIFI = 1, CMD_STATE = 2, CMD_INFO = 3, CMD_SCAN = 4 };
enum : uint8_t { ERR_NONE = 0, ERR_INVALID = 1, ERR_UNKNOWN = 2, ERR_CONNECT = 3 };

static void improvSend(uint8_t type, const uint8_t* data, uint8_t len) {
  uint8_t pkt[9 + 256];
  memcpy(pkt, "IMPROV", 6);
  pkt[6] = 1;
  pkt[7] = type;
  pkt[8] = len;
  memcpy(pkt + 9, data, len);
  uint8_t sum = 0;
  for (int i = 0; i < 9 + len; i++) sum += pkt[i];
  pkt[9 + len] = sum;
  Serial.write(pkt, 10 + len);
  Serial.write('\n');
  Serial.flush();
}

static void improvState(uint8_t s) { improvSend(IMP_STATE, &s, 1); }
static void improvError(uint8_t e) { improvSend(IMP_ERROR, &e, 1); }

// RPC result: command | total length | (len | string)...
static void improvResult(uint8_t cmd, std::initializer_list<String> strs) {
  uint8_t data[256];
  size_t n = 2;
  for (const String& s : strs) {
    if (n + 1 + s.length() > sizeof data) break;
    data[n++] = s.length();
    memcpy(data + n, s.c_str(), s.length());
    n += s.length();
  }
  data[0] = cmd;
  data[1] = n - 2;
  improvSend(IMP_RESULT, data, n);
}

static void improvRpc(const uint8_t* d, uint8_t len) {
  if (len < 2) { improvError(ERR_INVALID); return; }
  uint8_t cmd = d[0];
  switch (cmd) {
    case CMD_STATE:
      improvState(netConnected() ? ST_PROVISIONED : ST_READY);
      if (netConnected()) improvResult(CMD_STATE, {netUrl()});
      break;
    case CMD_INFO:
      improvResult(CMD_INFO, {FW_NAME, FW_VERSION, "ESP32", settings.name});
      break;
    case CMD_SCAN: {
      int found = WiFi.scanNetworks();
      for (int i = 0; i < found; i++) {
        improvResult(CMD_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)),
                                WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      }
      WiFi.scanDelete();
      uint8_t end[2] = {CMD_SCAN, 0};
      improvSend(IMP_RESULT, end, 2);
      break;
    }
    case CMD_WIFI: {
      // data: cmd | len | ssid_len | ssid | pass_len | pass
      const uint8_t* p = d + 2;
      uint8_t sl = p[0];
      if (4 + sl > len) { improvError(ERR_INVALID); return; }
      String ssid((const char*)p + 1, sl);
      uint8_t pl = p[1 + sl];
      if (4 + sl + pl > len) { improvError(ERR_INVALID); return; }
      String pass((const char*)p + 2 + sl, pl);
      improvState(ST_PROVISIONING);
      if (netJoin(ssid, pass)) {
        improvState(ST_PROVISIONED);
        improvResult(CMD_WIFI, {netUrl()});
      } else {
        improvError(ERR_CONNECT);
        improvState(ST_READY);
      }
      break;
    }
    default:
      improvError(ERR_UNKNOWN);
  }
}

static uint8_t ib[9 + 256 + 1];
static size_t il = 0;

void improvFeed(uint8_t b, void (*console)(uint8_t)) {
  static const char* HDR = "IMPROV";
  if (il < 6) {
    if (b == (uint8_t)HDR[il]) { ib[il++] = b; return; }
    // Not Improv after all: hand the held bytes and this one to the console
    for (size_t i = 0; i < il; i++) console(ib[i]);
    il = 0;
    if (b == 'I') { ib[il++] = b; return; }
    console(b);
    return;
  }
  ib[il++] = b;
  if (il < 9) return;
  size_t total = 9 + ib[8] + 1;
  if (il < total) return;
  il = 0;
  uint8_t sum = 0;
  for (size_t i = 0; i < total - 1; i++) sum += ib[i];
  if (ib[6] != 1 || sum != ib[total - 1]) { improvError(ERR_INVALID); return; }
  if (ib[7] == IMP_RPC) improvRpc(ib + 9, ib[8]);
}
