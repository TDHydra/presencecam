#include "sensors.h"

#include <Wire.h>

static const int MAX_SENSORS = 8;
static Sensor* list[MAX_SENSORS];
static int count = 0;

static Sensor* makeSensor(const String& type) {
  if (type == "ld2410") return makeLD2410();
  if (type == "vl53l0x") return makeVL53L0X();
  return nullptr;
}

void sensorsBegin() {
  for (JsonVariantConst cfg : hw["sensors"].as<JsonArrayConst>()) {
    if (count == MAX_SENSORS) break;
    Sensor* s = makeSensor(cfg["type"] | "");
    if (!s) continue;
    s->ok = s->begin(cfg);
    Serial.printf("sensor: %s %s\n", s->title(), s->ok ? "ready" : "NOT FOUND");
    list[count++] = s;
  }
  if (!count) Serial.println("sensor: none configured (pick some in the web flasher)");
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
