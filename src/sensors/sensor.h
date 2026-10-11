// Sensor modules. Each sensor type in the hardware config maps to one class here.
// To add a sensor: write a class implementing Sensor, add it to makeSensor() in
// sensors.cpp, accept it in hwconfig.cpp validate(), and add catalog/sensors/<type>.json.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct Reading {
  const char* label;  // "Distance"
  float value;
  const char* unit;   // "cm"
  float max;          // for bars; 0 = no bar
  uint8_t decimals;
  bool valid;         // false = no data right now (shown as "--")
  bool boolean;       // value is 0/1, shown as yes/no
};

class Sensor {
 public:
  virtual ~Sensor() {}
  virtual bool begin(JsonVariantConst cfg) = 0;  // false if the part didn't answer
  virtual void loop() = 0;
  // Fills up to max readings, returns how many.
  virtual int readings(Reading* out, int max) = 0;
  virtual bool givesPresence() { return false; }
  virtual bool presence() { return false; }
  virtual String presenceDetail() { return ""; }  // goes into the "present" log line
  virtual const char* type() = 0;                 // "ld2410"
  virtual const char* title() = 0;                // "LD2410 radar"
  virtual String status() = 0;                    // one line for the console / web page
  bool ok = false;                                // begin() succeeded
};

// --- the set of fitted sensors (built from hw["sensors"]) ---
void sensorsBegin();
void sensorsLoop();
int sensorsCount();
Sensor* sensorAt(int i);
bool sensorsGivePresence();  // at least one fitted sensor can detect presence
bool sensorsPresence();
String sensorsPresenceDetail();
// Calls cb for every reading of every sensor, in config order.
void sensorsForEachReading(void (*cb)(Sensor& s, const Reading& r, void* ctx), void* ctx);
