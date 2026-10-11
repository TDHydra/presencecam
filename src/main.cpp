// PresenceCam: radar presence sensor with an OLED, a web dashboard and a tamper-evident
// event log, for an ESP32 NodeMCU (ESP-32S) board. See README.md for wiring.
#include <Arduino.h>
#include <esp_system.h>

#include "config.h"
#include "console.h"
#include "display.h"
#include "eventlog.h"
#include "net.h"
#include "presence.h"
#include "radar.h"
#include "settings.h"
#include "web.h"

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (weak USB power?)";
    case ESP_RST_EXT: return "reset button";
    default: return "other";
  }
}

// BOOT button: short press = next OLED page, hold 5 s = forget Wi-Fi and open setup.
static void buttonLoop() {
  static bool was = false;
  static uint32_t downAt = 0;
  static bool longDone = false;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();
  if (down && !was) { downAt = now; longDone = false; }
  if (down && !longDone && now - downAt > 5000) {
    longDone = true;
    displayMessage("Wi-Fi reset", "restarting...", 3000);
    Serial.println("button: held 5 s, forgetting Wi-Fi");
    settingsForgetWifi();
    delay(1500);
    ESP.restart();
  }
  if (!down && was && !longDone && now - downAt > 30) displayNextPage();
  was = down;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  settingsLoad();
  displayBegin();
  displayMessage(FW_NAME, FW_VERSION, 1500);
  displayLoop();
  radarBegin();
  logBegin();
  logEvent("boot", String("fw ") + FW_VERSION + ", " + resetReason());
  netBegin();
  webBegin();
  printBanner(resetReason());
}

void loop() {
  while (Serial.available()) improvFeed(Serial.read(), consoleByte);
  radarLoop();
  presenceLoop();
  buttonLoop();
  netLoop();
  webLoop();
  displayLoop();
  consoleLoop();
  delay(2);
}
