#include "rules.h"
#include "debuglog.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "camera/camera.h"
#include "eventlog.h"
#include "presence.h"
#include "sensors/sensor.h"

namespace {

struct Rule {
  String when, op, action;
  float value;
  uint32_t everyMs;
  // runtime
  bool wasTrue = false;
  uint32_t fires = 0, lastFireMs = 0;
};

Rule rules[MAX_RULES];
int count = 0;
String stored = "[]";
uint32_t lastEvalMs = 0;

// Finds the current value of what a rule watches. Returns false when there is no such
// reading or it has no valid value right now.
bool currentValue(const String& when, float& out) {
  if (when.equalsIgnoreCase("presence")) {
    out = presence().present ? 1 : 0;
    return true;
  }
  struct Ctx { const String* when; float v; bool found; } ctx{&when, 0, false};
  sensorsForEachReading([](Sensor&, const Reading& r, void* p) {
    Ctx& c = *(Ctx*)p;
    if (c.found || !c.when->equalsIgnoreCase(r.label) || !r.valid) return;
    c.v = r.value;
    c.found = true;
  }, &ctx);
  out = ctx.v;
  return ctx.found;
}

bool test(const Rule& r, float v) {
  if (r.op == "<") return v < r.value;
  if (r.op == ">") return v > r.value;
  return (v != 0) == (r.value != 0);  // "is"
}

String describe(const Rule& r) {
  String v = r.op == "is" ? (r.value != 0 ? "yes" : "no") : String(r.value, 0);
  return r.when + " " + r.op + " " + v + " -> " + r.action;
}

void fire(Rule& r) {
  r.fires++;
  r.lastFireMs = millis();
  if (r.action == "photo") {
    String path = cameraCapture(describe(r));
    logEvent("photo", path.length() ? path + " (" + describe(r) + ")" : "FAILED: " + cameraStatus());
  } else if (r.action == "log") {
    logEvent("rule", describe(r));
  }
}

String parse(const String& json, Rule* out, int& n) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return "rules: not valid JSON";
  if (!doc.is<JsonArray>()) return "rules: want a JSON array";
  n = 0;
  for (JsonVariantConst r : doc.as<JsonArrayConst>()) {
    if (n == MAX_RULES) return "rules: at most " + String(MAX_RULES);
    Rule& o = out[n];
    o.when = r["when"] | "";
    o.op = r["op"] | "";
    o.action = r["do"] | "";
    o.value = r["value"] | 0.0f;
    o.everyMs = (r["every"] | 10) * 1000UL;
    if (!o.when.length()) return "rule " + String(n + 1) + ": \"when\" missing";
    if (o.op != "<" && o.op != ">" && o.op != "is") return "rule " + String(n + 1) + ": op must be <, > or is";
    if (o.action != "photo" && o.action != "led" && o.action != "log") return "rule " + String(n + 1) + ": do must be photo, led or log";
    if (o.action == "photo" && !cameraPresent()) return "rule " + String(n + 1) + ": no camera is set up";
    n++;
  }
  return "";
}

}  // namespace

void rulesBegin() {
  Preferences p;
  p.begin("pcam", true);
  String s = p.getString("rules", "[]");
  p.end();
  int n = 0;
  if (parse(s, rules, n).length()) {
    Serial.println("rules: stored rules are invalid, ignoring them");
    return;
  }
  count = n;
  stored = s;
  if (count) Serial.printf("rules: %d rule(s)\n", count);
}

String rulesSet(const String& json) {
  Rule tmp[MAX_RULES];
  int n = 0;
  String err = parse(json, tmp, n);
  if (err.length()) return err;
  for (int i = 0; i < n; i++) rules[i] = tmp[i];
  count = n;
  String compact;
  JsonDocument doc;
  deserializeJson(doc, json);
  serializeJson(doc, compact);
  stored = compact;
  Preferences p;
  p.begin("pcam", false);
  p.putString("rules", compact);
  p.end();
  logEvent("rules", String(count) + " rule(s) set");
  return "";
}

String rulesJson() { return stored; }

void rulesLoop() {
  uint32_t now = millis();
  if (now - lastEvalMs < 250) return;
  lastEvalMs = now;
  bool ledWanted = false, anyLed = false;
  for (int i = 0; i < count; i++) {
    Rule& r = rules[i];
    float v;
    bool isTrue = currentValue(r.when, v) && test(r, v);
    if (r.action == "led") {
      anyLed = true;
      ledWanted |= isTrue;
      if (isTrue && !r.wasTrue) r.fires++, r.lastFireMs = now;
    } else if (isTrue && !r.wasTrue && (r.fires == 0 || now - r.lastFireMs >= r.everyMs)) {
      fire(r);
    }
    r.wasTrue = isTrue;
  }
  presenceLedOverride(anyLed, ledWanted);
}

String rulesStatus() {
  String s;
  for (int i = 0; i < count; i++) {
    const Rule& r = rules[i];
    s += "  " + describe(r) + ": ";
    if (!r.fires) s += "never fired\n";
    else s += "fired " + String(r.fires) + "x, last " + String((millis() - r.lastFireMs) / 1000) + " s ago\n";
  }
  return s.length() ? s : "  none\n";
}
