// HC-SR04 ultrasonic distance sensor: a 10 us pulse on TRIG, then ECHO goes high for the
// sound's round trip (58 us per cm). Measured 5 times a second; pulseIn() blocks for at most
// 25 ms when nothing is in range. Optional "presenceCm" counts as presence below that distance.
#include "sensors.h"

namespace {

const uint32_t PERIOD_MS = 200;
const uint32_t TIMEOUT_US = 25000;  // ~430 cm round trip

class Ultrasonic : public Sensor {
  int trig = -1, echo = -1;
  float cm = 0;
  bool valid = false;
  int presenceCm = 0;
  uint32_t lastMs = 0;
  uint8_t misses = 0;

 public:
  bool begin(JsonVariantConst cfg) override {
    trig = hwPin(cfg, "trig");
    echo = hwPin(cfg, "echo");
    presenceCm = cfg["presenceCm"] | 0;
    if (trig < 0 || echo < 0) return false;
    pinMode(trig, OUTPUT);
    digitalWrite(trig, LOW);
    pinMode(echo, INPUT);
    return true;
  }

  void loop() override {
    if (!ok || millis() - lastMs < PERIOD_MS) return;
    lastMs = millis();
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    uint32_t us = pulseIn(echo, HIGH, TIMEOUT_US);
    if (us == 0 || us > TIMEOUT_US - 1000) {
      if (++misses >= 3) valid = false;  // one missed echo is normal, three means nothing there
      return;
    }
    misses = 0;
    float d = us / 58.0f;
    cm = valid ? cm * 0.5f + d * 0.5f : d;  // light smoothing
    valid = true;
  }

  int readings(Reading* r, int max) override {
    if (max < 1) return 0;
    r[0] = {"Ultrasonic", cm, "cm", 400, 0, ok && valid, false};
    return 1;
  }

  bool givesPresence() override { return presenceCm > 0; }
  bool presence() override { return presenceCm > 0 && ok && valid && cm < presenceCm; }
  String presenceDetail() override { return String(cm, 0) + " cm"; }
  const char* type() override { return "hcsr04"; }
  const char* title() override { return "HC-SR04 ultrasonic"; }
  String status() override {
    if (!ok) return "trig/echo pins not set";
    return valid ? String(cm, 0) + " cm" : String("nothing in range");
  }
};

}  // namespace

Sensor* makeHCSR04() { return new Ultrasonic(); }
