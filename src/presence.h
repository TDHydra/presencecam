// Turns raw radar detections into a steady present / clear state with a hold time,
// and logs each change.
#pragma once
#include <Arduino.h>

struct Presence {
  bool present;
  uint32_t sinceMs;        // when the current state started
  uint32_t lastSeenMs;     // last raw detection
  uint32_t visitsToday;    // "present" events since boot (or since midnight once the clock is set)
};

void presenceLoop();
const Presence& presence();
