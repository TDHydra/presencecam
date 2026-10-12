#include "health.h"

#include <esp_attr.h>
#include <esp_system.h>

#include "camera/camera.h"
#include "config.h"
#include "eventlog.h"
#include "hwconfig.h"
#include "net.h"
#include "power.h"
#include "sensors/sensor.h"
#include "settings.h"
#include "storage.h"

static const uint32_t MAGIC = 0x50434D43;  // "PCMC"
static const uint32_t CRASHES_FOR_SAFE_MODE = 3;
static const uint32_t SETTLED_MS = 60000;  // up this long = the crash streak is over

// Crashes in a row, kept in RTC memory (survives a crash reset, not a power cycle)
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcCrashes;

static bool counted = false;
static uint32_t crashes = 0;
static bool settled = false;
static uint32_t lastRetry = 0;

static void countCrashes() {
  if (counted) return;
  counted = true;
  esp_reset_reason_t why = esp_reset_reason();
  bool crash = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  bool valid = rtcMagic == MAGIC;
  crashes = crash ? (valid ? rtcCrashes + 1 : 1) : 0;
  rtcMagic = MAGIC;
  rtcCrashes = crashes;
}

bool healthCrashLoop() {
  countCrashes();
  return crashes >= CRASHES_FOR_SAFE_MODE;
}

uint32_t healthCrashCount() {
  countCrashes();
  return crashes;
}

void healthBegin() {
  countCrashes();
  if (healthCrashLoop()) {
    Serial.printf("health: %lu crashes in a row, started in SAFE MODE (no sensors, default config)\n",
                  (unsigned long)crashes);
    logEvent("safemode", String(crashes) + " crashes in a row");
  }
}

void healthLoop() {
  uint32_t now = millis();
  if (!settled && now > SETTLED_MS) {
    settled = true;
    rtcCrashes = 0;  // ran a minute without crashing: a later crash starts a new count
  }
  if (now - lastRetry >= 30000) {
    lastRetry = now;
    int n = sensorsRetry();
    if (n) logEvent("sensor", String(n) + " sensor(s) found on retry");
  }
}

static String fixFor(Sensor& s, JsonVariantConst cfg) {
  String t = s.type();
  if (t == "ld2410")
    return "Check that the radar's TX goes to GPIO" + String(hwPin(cfg, "rx")) + " and its RX to GPIO" +
           String(hwPin(cfg, "tx")) + ", and that it gets 5 V (not 3.3 V).";
  if (t == "vl53l0x" || t == "vl53l1x") {
    JsonVariantConst i2c = hw["i2c"];
    return "Check SDA on GPIO" + String(hwPin(i2c, "sda")) + " and SCL on GPIO" + String(hwPin(i2c, "scl")) +
           ", and 3.3 V / GND to the sensor. Its XSHUT pin must not be pulled low.";
  }
  if (t == "hcsr04")
    return "Check TRIG on GPIO" + String(hwPin(cfg, "trig")) + " and ECHO on GPIO" + String(hwPin(cfg, "echo")) +
           " (through a divider: ECHO is 5 V), and 5 V to the module.";
  return "Check its wiring against the pin list under Hardware.";
}

int healthProblems(Problem* out, int max) {
  int n = 0;
  auto add = [&](const String& what, const String& fix) {
    if (n < max) out[n++] = Problem{what, fix};
  };
  if (hwSafeMode) {
    if (healthCrashLoop())
      add("Safe mode after " + String(crashes) + " crashes in a row",
          "The stored hardware config is ignored. Check the wiring of the last part you added, or pick a "
          "different setup in the web flasher. Restart to try the stored config again.");
    else
      add("Safe mode (RST pressed 3 times)", "The stored hardware config is ignored. Restart to use it again.");
  } else if (crashes) {
    add("Crashed " + String(crashes) + " time(s) before this start",
        "If it keeps happening, switch on debug mode and send the debug log.");
  }
  const char* chip = hw["chip"] | "";
  if (*chip && strcmp(chip, FW_CHIP) != 0)
    add(String("This setup was made for an ") + chip + ", but this is an " + FW_CHIP,
        "Pick this board in the web flasher and install again.");
  if (powerBrownouts())
    add(String(powerBrownouts()) + " brownout reset(s): the power supply sags" +
            (powerWifiAllowed() ? "" : ", so Wi-Fi is off"),
        "Use a shorter or thicker USB cable, a powered hub or a 5 V / 1 A supply.");
  for (int i = 0; i < sensorsCount(); i++) {
    Sensor* s = sensorAt(i);
    if (!s->ok) add(String(s->title()) + " is not answering (retried every 30 s)", fixFor(*s, sensorConfig(i)));
  }
  if (!hw["camera"].isNull() && !cameraPresent())
    add("Camera: " + cameraStatus(), "Check the camera's ribbon cable or SPI wiring and its power.");
  if (!hw["sd"].isNull() && !storageOnSd())
    add("No microSD card", "Insert a FAT32-formatted card and restart. Until then files go to flash.");
  uint64_t total = storageTotal();
  if (!total) add("No storage mounted", "The flash file system failed. Use factory reset or reinstall the firmware.");
  else if (storageFree() < total / 10)
    add("Storage almost full: " + storageDescribe(), "Clear the debug log or old photos.");
  if (ESP.getFreeHeap() < 20 * 1024)
    add("Low memory: " + String(ESP.getFreeHeap() / 1024) + " KB free",
        "Fewer sensors or a smaller photo size help. If it keeps falling, send the debug log.");
  if (!netConnected() && !netApActive() && millis() > 60000) {
    if (netWired()) add("Ethernet has no link or no address", "Check the cable and the switch port (PoE), or set a fixed address.");
    else if (settings.wifiSsid.length() && powerWifiAllowed())
      add("Can't join Wi-Fi \"" + settings.wifiSsid + "\"", "Check the password and that the network is 2.4 GHz.");
  }
  return n;
}

String healthSummary() {
  Problem list[8];
  int n = healthProblems(list, 8);
  if (!n) return "OK";
  String s = String(n) + " problem" + (n > 1 ? "s" : "") + ": ";
  for (int i = 0; i < n; i++) s += (i ? "; " : "") + list[i].what;
  return s;
}
