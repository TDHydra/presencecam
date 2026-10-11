// Firmware-wide constants and defaults. Pins are not here: they come from the hardware
// config (src/hwconfig.h), which the web flasher writes for the board and parts you chose.
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#define FW_NAME "PresenceCam"

#if CONFIG_IDF_TARGET_ESP32S3
#define FW_CHIP "ESP32-S3"
#else
#define FW_CHIP "ESP32"
#endif

// Defaults, all changeable from the web page or the serial console
#define DEFAULT_HOLD_S 15         // stay "present" this long after the last detection
#define DEFAULT_MAX_CM 600        // LD2410: ignore targets further than this
#define DEFAULT_SCREEN_SLEEP_S 0  // 0 = screen always on

#define LOG_ROTATE_BYTES 48000  // log.txt moves to log.old.txt at this size
