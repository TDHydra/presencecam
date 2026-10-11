#include "presence.h"

#include <time.h>

#include "eventlog.h"
#include "hwconfig.h"
#include "sensors/sensor.h"
#include "settings.h"

static Presence p{false, 0, 0, 0};
static int today = -1;
static int ledPin = -1;
static bool ledRgb = false;

static void setLed(bool on) {
  if (ledPin < 0) return;
  if (ledRgb) neopixelWrite(ledPin, on ? 40 : 0, on ? 20 : 0, 0);  // amber while present
  else digitalWrite(ledPin, on);
}

void presenceBegin() {
  ledPin = hwPin(hw.as<JsonVariantConst>(), "led");
  ledRgb = hw["ledType"] == "ws2812";
  if (ledPin >= 0 && !ledRgb) pinMode(ledPin, OUTPUT);
  setLed(false);
}

static void countVisit() {
  time_t now = time(nullptr);
  if (now > 1700000000) {  // clock is set
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_yday != today) {
      today = t.tm_yday;
      p.visitsToday = 0;
    }
  }
  p.visitsToday++;
}

void presenceLoop() {
  uint32_t now = millis();
  bool detected = sensorsPresence();
  if (detected) p.lastSeenMs = now;

  bool want = detected || (p.present && now - p.lastSeenMs < settings.holdSeconds * 1000UL);
  if (want == p.present) return;

  uint32_t lasted = (now - p.sinceMs) / 1000;
  p.present = want;
  p.sinceMs = now;
  setLed(want);

  if (want) {
    countVisit();
    logEvent("present", sensorsPresenceDetail());
  } else {
    logEvent("clear", "after " + String(lasted) + " s");
  }
}

const Presence& presence() { return p; }
