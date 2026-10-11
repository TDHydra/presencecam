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
// Display types: none, ssd1306, sh1106 (I2C OLEDs on the i2c pins), st7789 (SPI LCD, with
// mosi/sclk/cs/dc/rst/bl/w/h/xoff/yoff/invert). Sensor types: ld2410, vl53l0x.
// Without a stored config, the chip's built-in default is used (see defaultConfigJson()).
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
