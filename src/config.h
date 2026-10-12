// Firmware-wide constants and defaults. Pins are not here: they come from the hardware
// config (src/hwconfig.h), which the web flasher writes for the board and parts you chose.
#pragma once
#include <esp_arduino_version.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#define FW_NAME "PresenceCam"

#if CONFIG_IDF_TARGET_ESP32P4
#define FW_CHIP "ESP32-P4"
#elif CONFIG_IDF_TARGET_ESP32C6
#define FW_CHIP "ESP32-C6"
#elif CONFIG_IDF_TARGET_ESP32C3
#define FW_CHIP "ESP32-C3"
#elif CONFIG_IDF_TARGET_ESP32S3
#define FW_CHIP "ESP32-S3"
#else
#define FW_CHIP "ESP32"
#endif

// Which Arduino core the firmware was built with: "core 3" or "core 2"
#define FW_STR2(x) #x
#define FW_STR(x) FW_STR2(x)
#define FW_CORE "core " FW_STR(ESP_ARDUINO_VERSION_MAJOR)

// Defaults, all changeable from the web page or the serial console
#define DEFAULT_HOLD_S 15         // stay "present" this long after the last detection
#define DEFAULT_MAX_CM 600        // LD2410: ignore targets further than this
#define DEFAULT_SCREEN_SLEEP_S 0  // 0 = screen always on

#define LOG_ROTATE_BYTES 48000  // log.txt moves to log.old.txt at this size
