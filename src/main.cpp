// PresenceCam: shows sensor data on a small screen and the web, detects presence, and keeps
// a tamper-evident event log. The board, sensors, display and layout come from the hardware
// config written by the web flasher (see hwconfig.h). README.md has the details.
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>

#include "buttons.h"
#include "config.h"
#include "console.h"
#include "display.h"
#include "eventlog.h"
#include "hwconfig.h"
#include "net.h"
#include "power.h"
#include "presence.h"
#include "sensors/sensor.h"
#include "settings.h"
#include "web.h"

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on/RST";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (weak USB power?)";
    case ESP_RST_EXT: return "reset pin";
    default: return "other";
  }
}

void setup() {
  Serial.setRxBufferSize(2048);  // room for a whole "config set {...}" line
  Serial.begin(115200);
  powerBegin();
  int taps = buttonsCountResetTaps();

  settingsLoad();
  hwBegin(taps >= 3);
  JsonVariantConst i2c = hw["i2c"];
  if (hwPin(i2c, "sda") >= 0 && hwPin(i2c, "scl") >= 0) Wire.begin(hwPin(i2c, "sda"), hwPin(i2c, "scl"));

  displayBegin();
  displayMessage(FW_NAME, hwSafeMode ? "SAFE MODE" : FW_VERSION, 1500);
  displayLoop();
  sensorsBegin();
  presenceBegin();
  buttonsBegin();
  logBegin();
  netBegin();
  if (taps == 2) {
    Serial.println("rst: double tap, opening the setup network for 10 minutes");
    netStartSetupAp(10 * 60 * 1000);
  }
  webBegin();
  printBanner(resetReason());
}

// The "boot" log line is written once the board has run for 10 s. A board stuck in a reset
// loop (e.g. brownouts) then doesn't write to flash on every restart.
static void logBootOnce() {
  static bool done = false;
  if (done || millis() < 10000) return;
  done = true;
  String d = String("fw ") + FW_VERSION + ", " + resetReason();
  if (powerBrownouts()) d += " x" + String(powerBrownouts());
  if (!powerWifiAllowed()) d += ", Wi-Fi off (weak power)";
  if (hwSafeMode) d += ", safe mode";
  logEvent("boot", d);
}

void loop() {
  logBootOnce();
  powerLoop();
  while (Serial.available()) improvFeed(Serial.read(), consoleByte);
  sensorsLoop();
  presenceLoop();
  buttonsLoop();
  netLoop();
  webLoop();
  displayLoop();
  consoleLoop();
  delay(2);
}
