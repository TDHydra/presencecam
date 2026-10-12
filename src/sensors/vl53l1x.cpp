// VL53L1X laser time-of-flight distance sensor (I2C, address 0x29), up to about 4 m.
// Optional "presenceMm": counts as presence while something is closer than this.
#include <VL53L1X.h>
#include <Wire.h>

#include "sensors.h"

namespace {

class ToF : public Sensor {
  VL53L1X dev;
  uint16_t mm = 0;
  bool valid = false;
  int presenceMm = 0;
  uint32_t lastOk = 0;

 public:
  bool begin(JsonVariantConst cfg) override {
    presenceMm = cfg["presenceMm"] | 0;
    dev.setBus(&Wire);
    dev.setTimeout(100);
    if (!dev.init()) return false;
    dev.setDistanceMode(VL53L1X::Long);
    dev.setMeasurementTimingBudget(50000);
    dev.startContinuous(60);
    return true;
  }

  void loop() override {
    if (!ok) return;
    // read() blocks until a result is ready, so only ask when one is waiting
    if (!dev.dataReady()) {
      if (millis() - lastOk > 1000) valid = false;
      return;
    }
    uint16_t r = dev.read(false);
    if (dev.timeoutOccurred()) { valid = false; return; }
    lastOk = millis();
    valid = dev.ranging_data.range_status == VL53L1X::RangeValid && r < 4000;
    if (valid) mm = r;
  }

  int readings(Reading* r, int max) override {
    if (max < 1) return 0;
    r[0] = {"ToF", (float)mm, "mm", 4000, 0, ok && valid, false};
    return 1;
  }

  bool givesPresence() override { return presenceMm > 0; }
  bool presence() override { return presenceMm > 0 && ok && valid && mm < presenceMm; }
  String presenceDetail() override { return String(mm) + " mm"; }
  const char* type() override { return "vl53l1x"; }
  const char* title() override { return "VL53L1X distance"; }
  String status() override {
    if (!ok) return "not found on I2C (address 0x29)";
    return valid ? String(mm) + " mm" : String("nothing in range");
  }
};

}  // namespace

Sensor* makeVL53L1X() { return new ToF(); }
