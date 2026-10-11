#include "power.h"

#include <esp_attr.h>
#include <esp_system.h>

#include "settings.h"

static const uint32_t MAGIC = 0x50434D42;  // "PCMB"
static const int8_t LOW_POWER = 34;        // 8.5 dBm
static const int8_t FULL_POWER = 78;       // 19.5 dBm

// Survives software and brownout resets (not power-on)
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcBrownouts;

static uint32_t brownouts = 0;
static bool settled = false;

void powerBegin() {
  bool valid = rtcMagic == MAGIC;
  esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_BROWNOUT) brownouts = valid ? rtcBrownouts + 1 : 1;
  // A crash or watchdog reset keeps the count, so it can't restart a brownout loop at full
  // power. Power-on, RST and a deliberate restart start over.
  else if (valid && (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT))
    brownouts = rtcBrownouts;
  else brownouts = 0;
  rtcMagic = MAGIC;
  rtcBrownouts = brownouts;
}

uint32_t powerBrownouts() { return brownouts; }

bool powerWifiAllowed() { return brownouts < 2; }

int8_t powerTxQuarterDbm() {
  if (brownouts >= 1) return LOW_POWER;
  if (settings.wifiTxPower) return settings.wifiTxPower;
  return settings.lowPowerLatched ? LOW_POWER : FULL_POWER;
}

String powerSummary() {
  String s;
  if (!powerWifiAllowed()) s = "Wi-Fi OFF: USB power too weak (" + String(brownouts) + " brownouts)";
  else s = "Wi-Fi " + String(powerTxQuarterDbm() / 4.0f, 1) + " dBm";
  if (!settings.wifiTxPower) s += settings.lowPowerLatched ? ", auto (low after a brownout)" : ", auto";
  return s;
}

void powerLoop() {
  if (settled || millis() < 60000) return;
  settled = true;
  rtcBrownouts = 0;
  // Ran fine at low power after a brownout: remember it for the next start (auto mode only)
  if (brownouts == 1 && !settings.wifiTxPower && !settings.lowPowerLatched) {
    settings.lowPowerLatched = true;
    settingsSave();
    Serial.println("power: stable at low Wi-Fi power, using it from now on (set Wi-Fi power to undo)");
  }
}
