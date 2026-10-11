// Radar input. Works with an HLK-LD2410 (B/C) over UART, which gives distance and signal
// energy, or with any radar that only has a digital OUT pin (RCWL-0516, LD2410 without
// UART, HLK-LD1115...). If no UART frames arrive, the OUT pin alone decides.
#pragma once
#include <Arduino.h>

struct RadarState {
  bool uart;           // LD2410 frames are arriving
  bool outPin;         // level of the OUT pin
  bool moving;         // LD2410: moving target seen
  bool still;          // LD2410: stationary target seen
  uint16_t movingCm;
  uint16_t stillCm;
  uint16_t distanceCm; // LD2410's combined detection distance
  uint8_t movingEnergy;  // 0-100
  uint8_t stillEnergy;   // 0-100
  uint32_t frames;     // UART frames parsed since boot
  bool detected;       // raw detection after the distance limit, before the hold time
};

void radarBegin();
void radarLoop();  // call often; parses UART bytes and updates the state
const RadarState& radar();
const char* radarType();  // "LD2410 (UART)" or "OUT pin only"
