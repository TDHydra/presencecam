// Hardware config: which board, display, layout and sensors are fitted, and on which pins.
//
// It is one JSON document, normally written by the web flasher's configurator over USB
// ("config set {...}" on the serial console), stored in NVS, and read at boot. Example:
//
// {"v":1,"board":"esp32-devkit-30","led":2,"button":0,     ("ledType":"ws2812" for an RGB LED)
//  "i2c":{"sda":21,"scl":22},
//  "display":{"type":"ssd1306","addr":60},
//  "layout":"basic",
//  "sensors":[{"type":"ld2410","uart":2,"rx":16,"tx":17,"out":27},{"type":"vl53l0x","presenceMm":0}],
//  "buttons":{"short":"next","double":"dim","hold3":"info","hold10":"forget_wifi"}}
//
// Optional: "outputs":[{"gpio":41,"level":1}] are driven at boot (power hold, RF switch...);
// "eth" makes the board use Ethernet instead of Wi-Fi: {"type":"w5500","sclk","mosi","miso",
// "cs","int","rst","spiMhz"} or {"type":"ip101","phyAddr","mdc","mdio","power"} (ESP32-P4).
// "wifiHosted":{"clk","cmd","d0","d1","d2","d3","rst"}: SDIO pins of an ESP32-C6 that gives an
// ESP32-P4 its Wi-Fi (ESP-Hosted).
// "camera" (optional): {"type":"arducam_mega","cs","sck","mosi","miso"} (Arducam Mega on SPI),
// {"type":"esp32cam"} (AI-Thinker ESP32-CAM) or {"type":"p4_csi","sensor":"auto","sda","scl"}
// (ESP32-P4 MIPI-CSI). See camera/camera.h.
// "ledType": "gpio" (default, lit when high), "gpio_low" (lit when low) or "ws2812".
// Display types: none, ssd1306, sh1106 (I2C OLEDs on the i2c pins), st7789 (SPI LCD, with
// mosi/sclk/cs/dc/rst/bl/w/h/xoff/yoff/invert). Sensor types: ld2410, vl53l0x, vl53l1x,
// hcsr501 (PIR on "out"), hcsr04 (ultrasonic on "trig"/"echo").
// "sd" (optional): microSD slot, see storage.h. "chip": the chip the setup was made for (the
// health check warns on a mismatch).
// Each sensor may have "show": {"Distance":"web","Still":"off",...}: where each reading appears,
// "both" (default), "screen", "web" or "off" (keyed by the reading's label).
// "web" (optional): the device's web page: {"title","theme":"auto|dark|light","accent":"#rrggbb",
// "style":"cards|list|big","hide":["log","rules","photos","hardware","health","debug"],
// "admin":"open|collapsed"}.
// Where the config comes from at boot: a setup built into the image by the web flasher (the
// first time that image runs), else the one stored in NVS, else the chip's built-in default
// (see defaultConfigJson()).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

extern JsonDocument hw;      // the active config; read it, don't change it
extern bool hwSafeMode;      // booted in safe mode: default config, no sensors

void hwBegin(bool safeMode);  // loads the stored config (or the default)
const char* defaultConfigJson();

// Validates and stores a new config. Returns "" on success, otherwise what is wrong.
String hwSet(const String& json);
void hwReset();               // back to the built-in default
String hwJson();              // the active config, compact JSON
uint32_t hwHash();            // CRC32 of the active config, shown after "config ok"

// Helpers
int hwPin(JsonVariantConst obj, const char* key, int fallback = -1);
const char* hwBoardName();

// Every GPIO the config uses, for the web page and console: "i2c sda" -> 21 ...
void hwForEachPin(void (*cb)(const String& what, int gpio, void* ctx), void* ctx);
