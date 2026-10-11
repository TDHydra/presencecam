// Pins and defaults. Change the pins here if you wired things differently.
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#define FW_NAME "PresenceCam"

// OLED (SSD1306 0.96" or SH1106 1.3", 128x64, I2C)
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// Radar. LD2410: TX -> GPIO16, RX -> GPIO17, OUT -> GPIO27.
// RCWL-0516 or any radar with a digital output: OUT -> GPIO27 only.
#define PIN_RADAR_RX 16  // ESP32 receives here (wire to the radar's TX)
#define PIN_RADAR_TX 17  // ESP32 transmits here (wire to the radar's RX)
#define PIN_RADAR_OUT 27
#define RADAR_BAUD 256000

#define PIN_BUTTON 0  // the board's BOOT button
#define PIN_LED 2     // on-board blue LED, lit while someone is present

// Defaults, all changeable from the web page or the serial console
#define DEFAULT_HOLD_S 15       // stay "present" this long after the last detection
#define DEFAULT_MAX_CM 600      // LD2410 only: ignore targets further than this
#define DEFAULT_OLED_SLEEP_S 0  // 0 = screen always on

#define LOG_ROTATE_BYTES 48000  // log.txt moves to log.old.txt at this size
