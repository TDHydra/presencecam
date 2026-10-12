#include "net.h"
#include "debuglog.h"

#include <ESPmDNS.h>
#if HAS_ETH
#include <ETH.h>
#include <SPI.h>
#endif
#if HAS_WIFI
#include <DNSServer.h>
#include <WiFi.h>
#endif

#include "config.h"
#include "display.h"
#include "eventlog.h"
#include "hwconfig.h"
#include "power.h"
#include "settings.h"

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

String netUrl() { return netConnected() ? "http://" + netIp() + "/" : ""; }

// Just got an address: tell the console, screen and log, and (re)start mDNS
static void announce(const char* tag, const char* name) {
  Serial.printf("%s: connected, %s  (http://%s.local)\n", tag, netUrl().c_str(), netHostname().c_str());
  MDNS.end();
  if (MDNS.begin(netHostname().c_str())) MDNS.addService("http", "tcp", 80);
  displayMessage(String(name) + " connected", netIp(), 5000);
  logEvent(tag, netIp());
}

static bool wired = false;  // the hardware config has an "eth" block: Ethernet instead of Wi-Fi

String netAddressing() {
  if (!settings.staticIp) return "DHCP";
  return "static " + settings.ip + " mask " + settings.mask + " via " + settings.gateway + (settings.dns.length() ? ", DNS " + settings.dns : "");
}

// Parses the static addresses from settings. "" when fine (or when DHCP is used).
static String parseAddresses(IPAddress& ip, IPAddress& mask, IPAddress& gw, IPAddress& dns) {
  if (!settings.staticIp) return "";
  if (!ip.fromString(settings.ip)) return "IP address \"" + settings.ip + "\" is not valid";
  if (!mask.fromString(settings.mask)) return "subnet mask \"" + settings.mask + "\" is not valid";
  if (!gw.fromString(settings.gateway)) return "gateway \"" + settings.gateway + "\" is not valid";
  if (settings.dns.length() && !dns.fromString(settings.dns)) return "DNS \"" + settings.dns + "\" is not valid";
  if (!settings.dns.length()) dns = gw;
  return "";
}
static bool wasConnected = false;

bool netWired() { return wired; }

// ---------------------------------------------------------------------------------------
// Ethernet: the chip's own MAC with an RMII PHY (ESP32-P4: IP101), or a W5500 on SPI

#if HAS_ETH
static bool ethUp() { return ETH.connected() && ETH.hasIP(); }

static void ethBegin() {
  JsonVariantConst e = hw["eth"];
  String type = e["type"] | "";
  ETH.setHostname(netHostname().c_str());
  netApplyAddressing();
  bool ok = false;
  if (type == "w5500") {
    SPI.begin(hwPin(e, "sclk"), hwPin(e, "miso"), hwPin(e, "mosi"));
    ok = ETH.begin(ETH_PHY_W5500, 1, hwPin(e, "cs"), hwPin(e, "int"), hwPin(e, "rst"), SPI, e["spiMhz"] | 20);
  }
#if CONFIG_IDF_TARGET_ESP32P4
  else if (type == "ip101") {
    ok = ETH.begin(ETH_PHY_IP101, e["phyAddr"] | 1, hwPin(e, "mdc"), hwPin(e, "mdio"), hwPin(e, "power"), EMAC_CLK_EXT_IN);
  }
#endif
  if (!ok) {
    Serial.printf("eth: %s Ethernet did not start\n", type.c_str());
    displayMessage("Ethernet failed", type, 10000);
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("eth: waiting for a cable and DHCP");
}

static void ethLoop() {
  bool up = ethUp();
  if (up && !wasConnected) {
    wasConnected = true;
    announce("eth", "Ethernet");
  } else if (!up && wasConnected) {
    wasConnected = false;
    Serial.println("eth: link lost");
  }
}

static String ethLink() { return "Ethernet " + String(ETH.linkSpeed()) + "M" + (ETH.fullDuplex() ? "" : " half duplex"); }
static String ethIp() { return ETH.localIP().toString(); }
#else
// hwconfig refuses an "eth" block in these builds, so none of this runs
static bool ethUp() { return false; }
static void ethBegin() {}
static void ethLoop() {}
static String ethLink() { return ""; }
static String ethIp() { return ""; }
#endif

#if HAS_WIFI
// ---------------------------------------------------------------------------------------
// Wi-Fi: the saved network, or a setup access point with a captive portal

static DNSServer dns;
static bool apOn = false;
static uint32_t lostSince = 0;
static uint32_t apOffAt = 0;  // close the setup AP a while after we got online
static bool radioOff = false;  // Wi-Fi disabled for this boot because power is too weak

static void applyTxPower() {
  WiFi.setTxPower((wifi_power_t)powerTxQuarterDbm());
}

static bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

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

static void wifiBegin() {
#if !SOC_WIFI_SUPPORTED
  // No radio on the chip: Wi-Fi goes through a co-processor, which needs its SDIO pins
  JsonVariantConst h = hw["wifiHosted"];
  if (h.isNull()) {
    radioOff = true;
    Serial.println("net: no Ethernet or Wi-Fi co-processor in the hardware config");
    return;
  }
  WiFi.setPins(hwPin(h, "clk"), hwPin(h, "cmd"), hwPin(h, "d0"), hwPin(h, "d1"), hwPin(h, "d2"), hwPin(h, "d3"), hwPin(h, "rst"));
#endif
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
  netApplyAddressing();
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

static void wifiLoop() {
  if (radioOff) return;
  if (apOn) dns.processNextRequest();
  uint32_t now = millis();
  bool up = wifiUp();

  if (up && !wasConnected) {
    wasConnected = true;
    announce("wifi", "Wi-Fi");
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

bool netConnected() { return wired ? ethUp() : wifiUp(); }
bool netApActive() { return apOn; }
String netIp() {
  if (!netConnected()) return "";
  return wired ? ethIp() : WiFi.localIP().toString();
}
String netLink() { return wired ? ethLink() : WiFi.SSID(); }
int netRssi() { return !wired && wifiUp() ? WiFi.RSSI() : 0; }
void netForget() {
  if (!wired) WiFi.disconnect(true);
}

int netScan(void (*cb)(const String& ssid, int rssi, bool open, void* ctx), void* ctx) {
  if (wired || radioOff) return 0;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) cb(WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN, ctx);
  WiFi.scanDelete();
  return max(n, 0);
}

void netApplyTxPower() {
  if (!wired && !radioOff) applyTxPower();
}

void netStartSetupAp(uint32_t ms) {
  if (wired) { Serial.println("eth: this board is set up for Ethernet; plug in a network cable"); return; }
  if (radioOff) return;
  startAp();
  apOffAt = millis() + ms;
}

bool netJoin(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (wired) {
    Serial.println("eth: this board is set up for Ethernet; plug in a network cable");
    return false;
  }
  if (radioOff) {
    Serial.println("wifi: off because of weak power; fix the power, then press RST");
    return false;
  }
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
    if (wifiUp()) return true;
    delay(100);
  }
  return wifiUp();
}

#else
// No Wi-Fi at all: Ethernet or nothing

bool netConnected() { return wired && ethUp(); }
bool netApActive() { return false; }
String netIp() { return netConnected() ? ethIp() : ""; }
String netLink() { return wired ? ethLink() : "no network"; }
int netRssi() { return 0; }
void netForget() {}
int netScan(void (*)(const String&, int, bool, void*), void*) { return 0; }
void netApplyTxPower() {}
void netStartSetupAp(uint32_t) { Serial.println("net: this board has no Wi-Fi; plug in a network cable"); }
bool netJoin(const String&, const String&, uint32_t) {
  Serial.println("net: this board has no Wi-Fi; plug in a network cable");
  return false;
}

#endif

String netApplyAddressing() {
  IPAddress ip, mask, gw, dns;
  String err = parseAddresses(ip, mask, gw, dns);
  if (err.length()) {
    Serial.println("net: " + err + ", using DHCP instead");
    ip = mask = gw = dns = IPAddress((uint32_t)0);
  }
  bool ok = true;
  if (wired) {
#if HAS_ETH
    ok = ETH.config(ip, gw, mask, dns);
#endif
  }
#if HAS_WIFI
  else ok = WiFi.config(ip, gw, mask, dns);
#endif
  if (!ok) return "the interface refused the addresses";
  if (!err.length() && settings.staticIp) Serial.println("net: " + netAddressing());
  return err;
}

void netBegin() {
  wired = !hw["eth"].isNull();
  if (wired) ethBegin();
#if HAS_WIFI
  else wifiBegin();
#endif
}

void netLoop() {
  if (wired) ethLoop();
#if HAS_WIFI
  else wifiLoop();
#endif
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
  if (wired) return;  // nothing to provision; staying quiet makes the installer skip its Wi-Fi step
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
      netScan([](const String& ssid, int rssi, bool open, void*) {
        improvResult(CMD_SCAN, {ssid, String(rssi), open ? "NO" : "YES"});
      }, nullptr);
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
