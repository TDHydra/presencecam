// HC-SR501 passive infrared motion sensor: OUT goes high (3.3 V) while it sees movement and
// for the time set on its delay knob. It needs about a minute after power-up to settle and
// may trigger on its own until then, so it doesn't count as presence during that time.
#include "sensors.h"

namespace {

const uint32_t WARMUP_MS = 60000;

class PIR : public Sensor {
  int pin = -1;
  bool motion = false;
  uint32_t triggers = 0;

  bool warmingUp() { return millis() < WARMUP_MS; }

 public:
  bool begin(JsonVariantConst cfg) override {
    pin = hwPin(cfg, "out");
    if (pin < 0) return false;
    pinMode(pin, INPUT_PULLDOWN);  // a loose wire reads as "no motion"
    return true;
  }

  void loop() override {
    if (!ok) return;
    bool m = digitalRead(pin);
    if (m && !motion && !warmingUp()) triggers++;
    motion = m;
  }

  int readings(Reading* r, int max) override {
    if (max < 1) return 0;
    r[0] = {"Motion", motion ? 1.0f : 0.0f, "", 0, 0, ok && !warmingUp(), true};
    return 1;
  }

  bool givesPresence() override { return true; }
  bool presence() override { return ok && motion && !warmingUp(); }
  String presenceDetail() override { return "PIR motion"; }
  const char* type() override { return "hcsr501"; }
  const char* title() override { return "HC-SR501 PIR"; }
  String status() override {
    if (!ok) return "no OUT pin set";
    if (warmingUp()) return "warming up (" + String((WARMUP_MS - millis()) / 1000) + " s)";
    return String(motion ? "motion" : "no motion") + ", " + String(triggers) + " trigger(s)";
  }
};

}  // namespace

Sensor* makePIR() { return new PIR(); }
