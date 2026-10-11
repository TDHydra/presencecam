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
  logEvent("boot", String("fw ") + FW_VERSION + ", " + resetReason() + (hwSafeMode ? ", safe mode" : ""));
  netBegin();
  if (taps == 2) {
    Serial.println("rst: double tap, opening the setup network for 10 minutes");
    netStartSetupAp(10 * 60 * 1000);
  }
  webBegin();
  printBanner(resetReason());
}

void loop() {
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
