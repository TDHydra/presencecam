#include "hwconfig.h"

#include <Preferences.h>
#include <esp_rom_crc.h>

#include "config.h"

JsonDocument hw;
bool hwSafeMode = false;

// Built-in defaults. ESP32: today's NodeMCU wiring (OLED on 21/22, LD2410 on 16/17/27).
// ESP32-S3: the Waveshare ESP32-S3-LCD-1.47B with its own LCD and no sensors yet.
// ESP32-P4: the Waveshare ESP32-P4-ETH with nothing attached.
#if CONFIG_IDF_TARGET_ESP32P4
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"waveshare-p4-eth","led":-1,"button":-1,"i2c":{"sda":-1,"scl":-1},)"
    R"("display":{"type":"none"},"layout":"basic","sensors":[]})";
#elif CONFIG_IDF_TARGET_ESP32S3
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"waveshare-s3-lcd-147b","led":38,"ledType":"ws2812","button":0,"i2c":{"sda":-1,"scl":-1},)"
    R"("display":{"type":"st7789","mosi":45,"sclk":40,"cs":42,"dc":41,"rst":39,"bl":46,"w":172,"h":320,"xoff":34,"yoff":0,"invert":true},)"
    R"("layout":"basic","sensors":[]})";
#else
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"esp32-devkit-30","led":2,"button":0,"i2c":{"sda":21,"scl":22},)"
    R"("display":{"type":"ssd1306","addr":60},"layout":"basic",)"
    R"("sensors":[{"type":"ld2410","uart":2,"rx":16,"tx":17,"out":27}]})";
#endif

const char* defaultConfigJson() { return DEFAULT_JSON; }

static const char* BUTTON_ACTIONS[] = {"none", "next", "dim", "info", "forget_wifi", "setup_ap", "reboot"};

int hwPin(JsonVariantConst obj, const char* key, int fallback) {
  JsonVariantConst v = obj[key];
  return v.is<int>() ? v.as<int>() : fallback;
}

const char* hwBoardName() { return hw["board"] | "unknown"; }

// --- pin rules per chip ---------------------------------------------------------------

static String pinProblem(int gpio, bool needsOutput) {
#if CONFIG_IDF_TARGET_ESP32P4
  if (gpio < 0 || gpio > 54) return "does not exist on the ESP32-P4";
  if ((gpio >= 28 && gpio <= 31) || gpio == 34 || gpio == 35 || (gpio >= 49 && gpio <= 52)) return "is used by Ethernet";
  if (gpio == 37 || gpio == 38) return "is the USB serial port";
  (void)needsOutput;
#elif CONFIG_IDF_TARGET_ESP32S3
  if (gpio < 0 || gpio > 48 || (gpio > 21 && gpio < 26)) return "does not exist on the ESP32-S3";
  if (gpio >= 26 && gpio <= 32) return "is used by the flash chip";
#ifdef BOARD_HAS_PSRAM
  if (gpio >= 33 && gpio <= 37) return "is used by the octal PSRAM";
#endif
  if (gpio == 19 || gpio == 20) return "is the USB port";
  (void)needsOutput;
#else
  if (gpio < 0 || gpio > 39 || gpio == 20 || gpio == 24 || (gpio >= 28 && gpio <= 31)) return "does not exist on the ESP32";
  if (gpio >= 6 && gpio <= 11) return "is used by the flash chip";
  if (gpio == 1 || gpio == 3) return "is the USB serial port";
  if (needsOutput && gpio >= 34) return "is input-only";
#endif
  return "";
}

struct Checker {
  String err;
  int used[64];
  String usedBy[64];
  int count = 0;

  // Records a pin. -1 means "not connected" and is fine when optional.
  void pin(int gpio, const String& what, bool needsOutput, bool optional = false) {
    if (err.length()) return;
    if (gpio == -1 && optional) return;
    String p = pinProblem(gpio, needsOutput);
    if (p.length()) { err = what + ": GPIO" + String(gpio) + " " + p; return; }
    for (int i = 0; i < count; i++) {
      if (used[i] == gpio) { err = what + ": GPIO" + String(gpio) + " is already used by " + usedBy[i]; return; }
    }
    if (count < 64) { used[count] = gpio; usedBy[count] = what; count++; }
  }
};

static String validate(JsonDocument& doc) {
  if (doc["v"].as<int>() != 1) return "unsupported config version (want \"v\":1)";
  Checker c;
  c.pin(hwPin(doc.as<JsonVariantConst>(), "led"), "status LED", true, true);
  c.pin(hwPin(doc.as<JsonVariantConst>(), "button"), "button", false, true);
  c.pin(hwPin(doc.as<JsonVariantConst>(), "hold"), "power hold", true, true);

  JsonVariantConst i2c = doc["i2c"].as<JsonVariantConst>();
  bool hasI2c = hwPin(i2c, "sda") >= 0 && hwPin(i2c, "scl") >= 0;
  if (hasI2c) {
    c.pin(hwPin(i2c, "sda"), "I2C SDA", true);
    c.pin(hwPin(i2c, "scl"), "I2C SCL", true);
  }

  JsonVariantConst d = doc["display"].as<JsonVariantConst>();
  String dt = d["type"] | "none";
  if (dt == "ssd1306" || dt == "sh1106") {
    if (!hasI2c) return "the " + dt + " display needs i2c pins";
  } else if (dt == "st7789") {
    for (const char* k : {"mosi", "sclk", "dc"}) c.pin(hwPin(d, k), String("display ") + k, true);
    for (const char* k : {"cs", "rst", "bl"}) c.pin(hwPin(d, k), String("display ") + k, true, true);
    if ((d["w"] | 0) < 32 || (d["h"] | 0) < 32) return "display w/h missing";
  } else if (dt != "none") {
    return "unknown display type \"" + dt + "\"";
  }

  String layout = doc["layout"] | "basic";
  if (layout != "basic") return "unknown layout \"" + layout + "\"";

  int n = 0;
  for (JsonVariantConst s : doc["sensors"].as<JsonArrayConst>()) {
    String t = s["type"] | "";
    String what = t + " #" + String(++n);
    if (t == "ld2410") {
      int rx = hwPin(s, "rx"), tx = hwPin(s, "tx"), out = hwPin(s, "out");
      int uart = s["uart"] | 2;
      if (uart < 1 || uart > 2) return what + ": uart must be 1 or 2";
      if (rx < 0 && out < 0) return what + ": needs rx/tx pins, an out pin, or both";
      c.pin(rx, what + " TX wire (ESP32 RX)", false, true);
      c.pin(tx, what + " RX wire (ESP32 TX)", true, true);
      c.pin(out, what + " OUT", false, true);
    } else if (t == "vl53l0x") {
      if (!hasI2c) return what + " needs i2c pins";
    } else {
      return "unknown sensor type \"" + t + "\"";
    }
    if (c.err.length()) return c.err;
  }

  for (JsonPairConst kv : doc["buttons"].as<JsonObjectConst>()) {
    bool ok = false;
    for (const char* a : BUTTON_ACTIONS) ok |= kv.value() == a;
    if (!ok) return String("unknown button action \"") + (kv.value() | "") + "\"";
  }
  return c.err;
}

// --- storage --------------------------------------------------------------------------

static void loadDefault() {
  hw.clear();
  deserializeJson(hw, DEFAULT_JSON);
}

void hwBegin(bool safeMode) {
  hwSafeMode = safeMode;
  loadDefault();
  if (safeMode) {
    hw["sensors"].to<JsonArray>();
    Serial.println("hw: SAFE MODE, built-in default config without sensors");
    return;
  }
  Preferences p;
  p.begin("pcamhw", true);
  String stored = p.getString("cfg", "");
  p.end();
  if (!stored.length()) return;

  JsonDocument doc;
  if (deserializeJson(doc, stored) || validate(doc).length()) {
    Serial.println("hw: stored config is invalid, using the built-in default");
    return;
  }
  hw = doc;
}

String hwSet(const String& json) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) return String("not valid JSON: ") + e.c_str();
  String err = validate(doc);
  if (err.length()) return err;
  String compact;
  serializeJson(doc, compact);
  Preferences p;
  p.begin("pcamhw", false);
  p.putString("cfg", compact);
  p.end();
  hw = doc;
  return "";
}

void hwReset() {
  Preferences p;
  p.begin("pcamhw", false);
  p.clear();
  p.end();
  loadDefault();
}

String hwJson() {
  String s;
  serializeJson(hw, s);
  return s;
}

uint32_t hwHash() {
  String s = hwJson();
  return esp_rom_crc32_le(0, (const uint8_t*)s.c_str(), s.length());
}

void hwForEachPin(void (*cb)(const String&, int, void*), void* ctx) {
  JsonVariantConst root = hw.as<JsonVariantConst>();
  auto emit = [&](const String& what, int gpio) {
    if (gpio >= 0) cb(what, gpio, ctx);
  };
  emit("status LED", hwPin(root, "led"));
  emit("button", hwPin(root, "button"));
  emit("power hold", hwPin(root, "hold"));
  emit("I2C SDA", hwPin(root["i2c"], "sda"));
  emit("I2C SCL", hwPin(root["i2c"], "scl"));
  JsonVariantConst d = root["display"];
  if (d["type"] == "st7789")
    for (const char* k : {"mosi", "sclk", "cs", "dc", "rst", "bl"}) emit(String("display ") + k, hwPin(d, k));
  int n = 0;
  for (JsonVariantConst s : root["sensors"].as<JsonArrayConst>()) {
    String t = String(s["type"] | "?") + " #" + String(++n);
    if (s["type"] == "ld2410") {
      emit(t + " TX -> ESP RX", hwPin(s, "rx"));
      emit(t + " RX -> ESP TX", hwPin(s, "tx"));
      emit(t + " OUT", hwPin(s, "out"));
    }
  }
}
