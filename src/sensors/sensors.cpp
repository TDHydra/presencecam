#include "sensors.h"
#include "../debuglog.h"

#include <Wire.h>

static const int MAX_SENSORS = 8;
static Sensor* list[MAX_SENSORS];
static int cfgIndex[MAX_SENSORS];  // position of each sensor in hw["sensors"]
static int count = 0;

static Sensor* makeSensor(const String& type) {
  if (type == "ld2410") return makeLD2410();
  if (type == "vl53l0x") return makeVL53L0X();
  if (type == "vl53l1x") return makeVL53L1X();
  if (type == "hcsr501") return makePIR();
  if (type == "hcsr04") return makeHCSR04();
  return nullptr;
}

void sensorsBegin() {
  int i = -1;
  for (JsonVariantConst cfg : hw["sensors"].as<JsonArrayConst>()) {
    i++;
    if (count == MAX_SENSORS) break;
    Sensor* s = makeSensor(cfg["type"] | "");
    if (!s) continue;
    s->ok = s->begin(cfg);
    Serial.printf("sensor: %s %s\n", s->title(), s->ok ? "ready" : "NOT FOUND");
    cfgIndex[count] = i;
    list[count++] = s;
  }
  if (!count) Serial.println("sensor: none configured (pick some in the web flasher)");
}

int sensorsRetry() {
  int found = 0;
  for (int i = 0; i < count; i++) {
    if (list[i]->ok) continue;
    list[i]->ok = list[i]->begin(hw["sensors"][cfgIndex[i]]);
    if (list[i]->ok) {
      Serial.printf("sensor: %s found on retry\n", list[i]->title());
      found++;
    }
  }
  return found;
}

JsonVariantConst sensorConfig(int i) {
  return i >= 0 && i < count ? hw["sensors"][cfgIndex[i]].as<JsonVariantConst>() : JsonVariantConst();
}

void sensorsLoop() {
  for (int i = 0; i < count; i++) list[i]->loop();
}

int sensorsCount() { return count; }
Sensor* sensorAt(int i) { return i >= 0 && i < count ? list[i] : nullptr; }

bool sensorsGivePresence() {
  for (int i = 0; i < count; i++)
    if (list[i]->givesPresence()) return true;
  return false;
}

bool sensorsPresence() {
  for (int i = 0; i < count; i++)
    if (list[i]->givesPresence() && list[i]->presence()) return true;
  return false;
}

String sensorsPresenceDetail() {
  for (int i = 0; i < count; i++)
    if (list[i]->givesPresence() && list[i]->presence()) return list[i]->presenceDetail();
  return "";
}

void sensorsForEachReading(void (*cb)(Sensor&, const Reading&, void*), void* ctx) {
  Reading r[8];
  for (int i = 0; i < count; i++) {
    int n = list[i]->readings(r, 8);
    for (int j = 0; j < n; j++) cb(*list[i], r[j], ctx);
  }
}

// sensors[i].show = {"Distance": "web", ...}: where each reading appears, "both" (default),
// "screen", "web" or "off". Keyed by the reading's label.
static bool shows(int i, const char* label, bool screen) {
  const char* where = hw["sensors"][cfgIndex[i]]["show"][label] | "both";
  if (!strcmp(where, "both")) return true;
  return !strcmp(where, screen ? "screen" : "web");
}

bool sensorShows(const Sensor* s, const char* label, bool screen) {
  for (int i = 0; i < count; i++)
    if (list[i] == s) return shows(i, label, screen);
  return true;
}

void sensorsForEachShown(bool screen, void (*cb)(Sensor&, const Reading&, void*), void* ctx) {
  Reading r[8];
  for (int i = 0; i < count; i++) {
    int n = list[i]->readings(r, 8);
    for (int j = 0; j < n; j++)
      if (shows(i, r[j].label, screen)) cb(*list[i], r[j], ctx);
  }
}
