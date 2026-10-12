#include "hwconfig.h"
#include "debuglog.h"

#include <Preferences.h>
#include <esp_rom_crc.h>
#include <soc/soc_caps.h>
#ifndef SOC_UART_HP_NUM  // IDF 4.4 (Arduino core 2)
#define SOC_UART_HP_NUM SOC_UART_NUM
#endif

#include "config.h"
#include "layouts/layout.h"

JsonDocument hw;
bool hwSafeMode = false;

// Built-in defaults. ESP32: today's NodeMCU wiring (OLED on 21/22, LD2410 on 16/17/27).
// ESP32-S3: the Waveshare ESP32-S3-LCD-1.47B with its own LCD and no sensors yet.
// ESP32-P4: the Waveshare ESP32-P4-ETH with nothing attached. ESP32-C3 / C6: the Seeed XIAO
// with nothing attached.
#if CONFIG_IDF_TARGET_ESP32P4
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"waveshare-p4-eth","chip":"ESP32-P4","led":-1,"button":-1,)"
    R"("eth":{"type":"ip101","phyAddr":1,"mdc":31,"mdio":52,"power":51},"i2c":{"sda":-1,"scl":-1},)"
    R"("sd":{"type":"sdmmc","clk":43,"cmd":44,"d0":39,"d1":40,"d2":41,"d3":42,"width":4,"power":45,"powerLevel":0},)"
    R"("display":{"type":"none"},"layout":"basic","sensors":[]})";
#elif CONFIG_IDF_TARGET_ESP32C3
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"xiao-esp32c3","chip":"ESP32-C3","led":-1,"button":9,"i2c":{"sda":-1,"scl":-1},)"
    R"("display":{"type":"none"},"layout":"basic","sensors":[]})";
#elif CONFIG_IDF_TARGET_ESP32C6
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"xiao-esp32c6","chip":"ESP32-C6","led":15,"ledType":"gpio_low","button":9,)"
    R"("outputs":[{"gpio":3,"level":0},{"gpio":14,"level":0}],"i2c":{"sda":-1,"scl":-1},)"
    R"("display":{"type":"none"},"layout":"basic","sensors":[]})";
#elif CONFIG_IDF_TARGET_ESP32S3
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"waveshare-s3-lcd-147b","chip":"ESP32-S3","led":38,"ledType":"ws2812","button":0,"i2c":{"sda":-1,"scl":-1},)"
    R"("display":{"type":"st7789","mosi":45,"sclk":40,"cs":42,"dc":41,"rst":39,"bl":46,"w":172,"h":320,"xoff":34,"yoff":0,"invert":true},)"
    R"("layout":"basic","sensors":[]})";
#else
static const char* DEFAULT_JSON =
    R"({"v":1,"board":"esp32-devkit-30","chip":"ESP32","led":2,"button":0,"i2c":{"sda":21,"scl":22},)"
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
  (void)needsOutput;
#if CONFIG_IDF_TARGET_ESP32C3
  if (gpio < 0 || gpio > 21) return "does not exist on the ESP32-C3";
  if (gpio >= 11 && gpio <= 17) return "is used by the flash chip";
  if (gpio == 18 || gpio == 19) return "is the USB port";
#elif CONFIG_IDF_TARGET_ESP32C6
  if (gpio < 0 || gpio > 30) return "does not exist on the ESP32-C6";
  if (gpio >= 24) return "is used by the flash chip";
  if (gpio == 12 || gpio == 13) return "is the USB port";
#elif CONFIG_IDF_TARGET_ESP32P4
  if (gpio < 0 || gpio > 54) return "does not exist on the ESP32-P4";
  if ((gpio >= 28 && gpio <= 31) || gpio == 34 || gpio == 35 || (gpio >= 49 && gpio <= 52)) return "is used by Ethernet";
  if (gpio == 37 || gpio == 38) return "is the USB serial port";
#elif CONFIG_IDF_TARGET_ESP32S3
  if (gpio < 0 || gpio > 48 || (gpio > 21 && gpio < 26)) return "does not exist on the ESP32-S3";
  if (gpio >= 26 && gpio <= 32) return "is used by the flash chip";
#ifdef BOARD_HAS_PSRAM
  if (gpio >= 33 && gpio <= 37) return "is used by the octal PSRAM";
#endif
  if (gpio == 19 || gpio == 20) return "is the USB port";
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
  for (JsonVariantConst o : doc["outputs"].as<JsonArrayConst>()) c.pin(hwPin(o, "gpio"), "boot output", true);

  JsonVariantConst hosted = doc["wifiHosted"].as<JsonVariantConst>();
  if (!hosted.isNull())
    for (const char* k : {"clk", "cmd", "d0", "d1", "d2", "d3", "rst"}) c.pin(hwPin(hosted, k), String("Wi-Fi co-processor ") + k, true);

  JsonVariantConst eth = doc["eth"].as<JsonVariantConst>();
  if (!eth.isNull()) {
#if ESP_ARDUINO_VERSION_MAJOR < 3
    return "Ethernet needs the Arduino core 3 firmware; install that one (or pick Wi-Fi)";
#endif
    String et = eth["type"] | "";
    if (et == "w5500") {
      for (const char* k : {"sclk", "mosi", "miso", "cs"}) c.pin(hwPin(eth, k), String("ethernet ") + k, true);
      for (const char* k : {"int", "rst"}) c.pin(hwPin(eth, k), String("ethernet ") + k, true, true);
    } else if (et == "ip101") {
#if !CONFIG_IDF_TARGET_ESP32P4
      return "the ip101 Ethernet PHY is only supported on the ESP32-P4";
#endif
    } else {
      return "unknown ethernet type \"" + et + "\"";
    }
  }

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

  JsonVariantConst cam = doc["camera"].as<JsonVariantConst>();
  if (!cam.isNull()) {
    String ct = cam["type"] | "";
    if (ct == "arducam_mega") {
      for (const char* k : {"cs", "sck", "mosi"}) c.pin(hwPin(cam, k), String("camera ") + k, true);
      c.pin(hwPin(cam, "miso"), "camera miso", false);
    } else if (ct == "esp32cam") {
#if !CONFIG_IDF_TARGET_ESP32
      return "the esp32cam camera is only on the AI-Thinker ESP32-CAM";
#endif
    } else if (ct == "p4_csi") {
#if !CONFIG_IDF_TARGET_ESP32P4
      return "the p4_csi camera is only on the ESP32-P4";
#endif
    } else {
      return "unknown camera type \"" + ct + "\"";
    }
  }

  JsonVariantConst sd = doc["sd"].as<JsonVariantConst>();
  if (!sd.isNull()) {
    String st = sd["type"] | "";
    if (st == "spi") {
      c.pin(hwPin(sd, "cs"), "SD card cs", true);
      // The card may share the SPI bus with the camera, a W5500 or the LCD: same sck/mosi/miso is fine.
      for (const char* k : {"sck", "mosi", "miso"}) {
        int g = hwPin(sd, k);
        const char* busKey = strcmp(k, "sck") ? k : "sclk";
        bool shared = (g >= 0 && (g == hwPin(cam, k) || g == hwPin(eth, busKey) || g == hwPin(d, busKey)));
        if (!shared) c.pin(g, String("SD card ") + k, strcmp(k, "miso") != 0);
      }
    } else if (st == "sdmmc") {
#if !SOC_SDMMC_HOST_SUPPORTED
      return "this chip has no SD/MMC host; use \"type\":\"spi\" for the card";
#endif
      int width = sd["width"] | 4;
      if (width != 1 && width != 4) return "sd width must be 1 or 4";
      for (const char* k : {"clk", "cmd", "d0"}) c.pin(hwPin(sd, k), String("SD card ") + k, true);
      if (width == 4)
        for (const char* k : {"d1", "d2", "d3"}) c.pin(hwPin(sd, k), String("SD card ") + k, true);
      c.pin(hwPin(sd, "power"), "SD card power", true, true);
    } else {
      return "unknown sd type \"" + st + "\"";
    }
  }

  // Where readings show (sensors[i].show) and how the web page looks ("web")
  for (JsonVariantConst sn : doc["sensors"].as<JsonArrayConst>())
    for (JsonPairConst kv : sn["show"].as<JsonObjectConst>()) {
      String w = kv.value() | "";
      if (w != "both" && w != "screen" && w != "web" && w != "off")
        return String("show.") + kv.key().c_str() + ": \"" + w + "\" (want both, screen, web or off)";
    }
  JsonVariantConst web = doc["web"].as<JsonVariantConst>();
  if (!web.isNull()) {
    String theme = web["theme"] | "auto", style = web["style"] | "cards", admin = web["admin"] | "open";
    if (theme != "auto" && theme != "dark" && theme != "light") return "web.theme must be auto, dark or light";
    if (style != "cards" && style != "list" && style != "big") return "web.style must be cards, list or big";
    if (admin != "open" && admin != "collapsed") return "web.admin must be open or collapsed";
    String accent = web["accent"] | "";
    if (accent.length() && (accent.length() != 7 || accent[0] != '#')) return "web.accent must look like #2563eb";
    if (strlen(web["title"] | "") > 40) return "web.title is longer than 40 characters";
  }

  String layout = doc["layout"] | "basic";
  bool known = false;
  for (const char* id : LAYOUT_IDS) known |= layout == id;
  if (!known) return "unknown layout \"" + layout + "\"";

  int n = 0;
  for (JsonVariantConst s : doc["sensors"].as<JsonArrayConst>()) {
    String t = s["type"] | "";
    String what = t + " #" + String(++n);
    if (t == "ld2410") {
      int rx = hwPin(s, "rx"), tx = hwPin(s, "tx"), out = hwPin(s, "out");
      int uart = s["uart"] | 2;
      if (uart < 1 || uart > SOC_UART_HP_NUM - 1) return what + ": uart must be 1" + String(SOC_UART_HP_NUM > 2 ? " or 2" : "");
      if (rx < 0 && out < 0) return what + ": needs rx/tx pins, an out pin, or both";
      c.pin(rx, what + " TX wire (ESP32 RX)", false, true);
      c.pin(tx, what + " RX wire (ESP32 TX)", true, true);
      c.pin(out, what + " OUT", false, true);
    } else if (t == "vl53l0x" || t == "vl53l1x") {
      if (!hasI2c) return what + " needs i2c pins";
    } else if (t == "hcsr04") {
      if (hwPin(s, "trig") < 0 || hwPin(s, "echo") < 0) return what + ": needs trig and echo pins";
      c.pin(hwPin(s, "trig"), what + " TRIG", true);
      c.pin(hwPin(s, "echo"), what + " ECHO", false);
    } else if (t == "hcsr501") {
      if (hwPin(s, "out") < 0) return what + ": needs an out pin";
      c.pin(hwPin(s, "out"), what + " OUT", false);
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

// --- the setup built into the image by the web flasher -----------------------------------
//
// site/generate.mjs finds this block in the downloaded firmware by its "PCAMCFG1" header,
// writes the setup's JSON into it and fixes the image checksum. Layout: magic, u32 length,
// u32 CRC32 of the JSON, the JSON. Length 0 (as built) = no setup in the image. `volatile`
// keeps the compiler from folding the reads to the build-time zeros.
static const size_t EMBED_SIZE = 8192;  // must match BLOCK_SIZE in site/generate.mjs
extern "C" __attribute__((used, aligned(4))) const volatile uint8_t pcamEmbeddedConfig[EMBED_SIZE] = {
    'P', 'C', 'A', 'M', 'C', 'F', 'G', '1'};

static uint32_t embU32(size_t at) {
  return pcamEmbeddedConfig[at] | (pcamEmbeddedConfig[at + 1] << 8) | (pcamEmbeddedConfig[at + 2] << 16) |
         ((uint32_t)pcamEmbeddedConfig[at + 3] << 24);
}

// The JSON in the block, or "" when there is none (or it is damaged). crc = its CRC32.
static String embeddedConfig(uint32_t& crc) {
  uint32_t len = embU32(8);
  if (!len || len > EMBED_SIZE - 16) return "";
  String json;
  json.reserve(len);
  for (uint32_t i = 0; i < len; i++) json += (char)pcamEmbeddedConfig[16 + i];
  crc = esp_rom_crc32_le(0, (const uint8_t*)json.c_str(), json.length());
  if (crc != embU32(12)) {
    Serial.println("hw: the setup built into this firmware is damaged, ignoring it");
    return "";
  }
  return json;
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
  // A setup built into the image wins once, the first time this image runs (a fresh install
  // or an update with a new setup). After that, a setup sent over USB or the web page wins
  // until the next image with a different built-in setup.
  uint32_t embCrc = 0;
  String embedded = embeddedConfig(embCrc);
  Preferences p;
  p.begin("pcamhw", false);
  if (embedded.length() && p.getUInt("emb", 0) != embCrc) {
    JsonDocument doc;
    String err = deserializeJson(doc, embedded) ? "not valid JSON" : validate(doc);
    if (err.length()) {
      Serial.println("hw: the setup built into this firmware is invalid (" + err + "), ignoring it");
    } else {
      p.putString("cfg", embedded);
      Serial.println("hw: using the setup built into this firmware");
    }
    p.putUInt("emb", embCrc);  // don't re-check (or re-apply) it on every boot
  }
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
  for (JsonVariantConst o : root["outputs"].as<JsonArrayConst>()) emit("boot output", hwPin(o, "gpio"));
  if (!root["wifiHosted"].isNull())
    for (const char* k : {"clk", "cmd", "d0", "d1", "d2", "d3", "rst"}) emit(String("Wi-Fi co-processor ") + k, hwPin(root["wifiHosted"], k));
  if (root["camera"]["type"] == "arducam_mega")
    for (const char* k : {"cs", "sck", "mosi", "miso"}) emit(String("camera ") + k, hwPin(root["camera"], k));
  if (root["eth"]["type"] == "w5500")
    for (const char* k : {"sclk", "mosi", "miso", "cs", "int", "rst"}) emit(String("ethernet ") + k, hwPin(root["eth"], k));
  JsonVariantConst sd = root["sd"];
  if (sd["type"] == "spi")
    for (const char* k : {"cs", "sck", "mosi", "miso"}) emit(String("SD card ") + k, hwPin(sd, k));
  else if (sd["type"] == "sdmmc")
    for (const char* k : {"clk", "cmd", "d0", "d1", "d2", "d3", "power"})
      if ((sd["width"] | 4) == 4 || k[0] != 'd' || k[1] == '0') emit(String("SD card ") + k, hwPin(sd, k));
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
    } else if (s["type"] == "hcsr501") {
      emit(t + " OUT", hwPin(s, "out"));
    } else if (s["type"] == "hcsr04") {
      emit(t + " TRIG", hwPin(s, "trig"));
      emit(t + " ECHO", hwPin(s, "echo"));
    }
  }
}
