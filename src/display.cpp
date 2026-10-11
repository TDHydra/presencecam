#include "display.h"

#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>

#include "config.h"
#include "eventlog.h"
#include "net.h"
#include "presence.h"
#include "radar.h"
#include "settings.h"

static U8G2* oled = nullptr;
static uint8_t addr = 0;
static uint8_t page = 0;
static const uint8_t PAGES = 3;
static uint32_t lastDraw = 0;
static String msg1, msg2;
static uint32_t msgUntil = 0;
static bool asleep = false;

bool displayFound() { return oled != nullptr; }
uint8_t displayAddress() { return addr; }

void displayBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  addr = 0;
  for (uint8_t a : {0x3C, 0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  delete oled;
  oled = nullptr;
  if (!addr) {
    Serial.println("oled: none found on SDA=21 SCL=22 (address 0x3C or 0x3D)");
    return;
  }
  if (settings.oledDriver == 1)
    oled = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  else
    oled = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  oled->setI2CAddress(addr * 2);
  oled->begin();
  oled->setDisplayRotation(settings.oledFlip ? U8G2_R2 : U8G2_R0);
  oled->setContrast(200);
  asleep = false;
  Serial.printf("oled: %s at 0x%02X\n", settings.oledDriver ? "SH1106" : "SSD1306", addr);
}

void displayNextPage() {
  page = (page + 1) % PAGES;
  lastDraw = 0;
}

void displayMessage(const String& a, const String& b, uint32_t ms) {
  msg1 = a;
  msg2 = b;
  msgUntil = millis() + ms;
  lastDraw = 0;
}

static String duration(uint32_t s) {
  char buf[16];
  if (s < 60) snprintf(buf, sizeof buf, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(buf, sizeof buf, "%lum %02lus", (unsigned long)s / 60, (unsigned long)s % 60);
  else snprintf(buf, sizeof buf, "%luh %02lum", (unsigned long)s / 3600, (unsigned long)(s / 60) % 60);
  return buf;
}

static void bar(int x, int y, int w, uint8_t pct) {
  oled->drawFrame(x, y, w, 6);
  oled->drawBox(x + 1, y + 1, (w - 2) * min<uint8_t>(pct, 100) / 100, 4);
}

static void header(const char* title) {
  oled->setFont(u8g2_font_6x10_tf);
  oled->drawStr(0, 8, title);
  // Wi-Fi indicator in the top-right corner
  const char* w = netConnected() ? "WiFi" : (netApActive() ? "AP" : "--");
  oled->drawStr(128 - oled->getStrWidth(w), 8, w);
  oled->drawHLine(0, 11, 128);
}

static void pageMain() {
  const Presence& p = presence();
  const RadarState& r = radar();
  header(settings.name.c_str());

  oled->setFont(u8g2_font_logisoso18_tr);
  const char* big = p.present ? "PRESENT" : "clear";
  oled->drawStr((128 - oled->getStrWidth(big)) / 2, 34, big);

  oled->setFont(u8g2_font_6x10_tf);
  char line[32];
  if (r.uart && p.present) {
    snprintf(line, sizeof line, "%u cm  %s", r.distanceCm, r.moving ? "moving" : "still");
  } else {
    snprintf(line, sizeof line, "for %s", duration((millis() - p.sinceMs) / 1000).c_str());
  }
  oled->drawStr((128 - oled->getStrWidth(line)) / 2, 46, line);

  if (r.uart) {
    oled->drawStr(0, 56, "mov");
    bar(22, 50, 40, r.movingEnergy);
    oled->drawStr(68, 56, "still");
    bar(98, 50, 30, r.stillEnergy);
  } else {
    snprintf(line, sizeof line, "radar OUT %s", r.outPin ? "HIGH" : "low");
    oled->drawStr(0, 56, line);
  }
  snprintf(line, sizeof line, "today %lu", (unsigned long)p.visitsToday);
  oled->drawStr(0, 64, line);
}

static void pageNetwork() {
  header("Network");
  oled->setFont(u8g2_font_6x10_tf);
  char line[40];
  if (netConnected()) {
    snprintf(line, sizeof line, "%s", WiFi.SSID().c_str());
    oled->drawStr(0, 24, line);
    snprintf(line, sizeof line, "%s", WiFi.localIP().toString().c_str());
    oled->setFont(u8g2_font_7x13B_tf);
    oled->drawStr(0, 38, line);
    oled->setFont(u8g2_font_6x10_tf);
    snprintf(line, sizeof line, "%s.local", netHostname().c_str());
    oled->drawStr(0, 50, line);
    snprintf(line, sizeof line, "signal %d dBm", WiFi.RSSI());
    oled->drawStr(0, 62, line);
  } else if (netApActive()) {
    oled->drawStr(0, 24, "Setup: join Wi-Fi");
    oled->setFont(u8g2_font_7x13B_tf);
    oled->drawStr(0, 38, netApName().c_str());
    oled->setFont(u8g2_font_6x10_tf);
    oled->drawStr(0, 50, "then open");
    oled->drawStr(0, 62, "http://192.168.4.1");
  } else {
    oled->drawStr(0, 30, "Connecting to");
    oled->drawStr(0, 42, settings.wifiSsid.c_str());
  }
}

static void pageInfo() {
  header("Device");
  oled->setFont(u8g2_font_6x10_tf);
  char line[40];
  snprintf(line, sizeof line, "fw %s", FW_VERSION);
  oled->drawStr(0, 23, line);
  snprintf(line, sizeof line, "radar %s", radarType());
  oled->drawStr(0, 34, line);
  snprintf(line, sizeof line, "log line %lu", (unsigned long)logNextLine() - 1);
  oled->drawStr(0, 45, line);
  snprintf(line, sizeof line, "up %s", duration(millis() / 1000).c_str());
  oled->drawStr(0, 56, line);
  snprintf(line, sizeof line, "hold %us  max %ucm", settings.holdSeconds, settings.maxCm);
  oled->drawStr(0, 64, line);
}

void displayLoop() {
  if (!oled) return;
  uint32_t now = millis();
  if (now - lastDraw < 250) return;
  lastDraw = now;

  // Optional screen sleep while nobody is around (protects the OLED from burn-in)
  bool wantSleep = settings.oledSleepS && !presence().present && now > msgUntil &&
                   now - presence().sinceMs > settings.oledSleepS * 1000UL;
  if (wantSleep != asleep) {
    asleep = wantSleep;
    oled->setPowerSave(asleep);
  }
  if (asleep) return;

  oled->clearBuffer();
  if ((int32_t)(msgUntil - now) > 0) {
    oled->setFont(u8g2_font_7x13B_tf);
    oled->drawStr((128 - oled->getStrWidth(msg1.c_str())) / 2, 28, msg1.c_str());
    oled->setFont(u8g2_font_6x10_tf);
    oled->drawStr((128 - oled->getStrWidth(msg2.c_str())) / 2, 46, msg2.c_str());
  } else if (page == 0) {
    pageMain();
  } else if (page == 1) {
    pageNetwork();
  } else {
    pageInfo();
  }
  oled->sendBuffer();
}
