#include "presence.h"

#include <time.h>

#include "config.h"
#include "eventlog.h"
#include "radar.h"
#include "settings.h"

static Presence p{false, 0, 0, 0};
static int today = -1;

static void countVisit() {
  time_t now = time(nullptr);
  if (now > 1700000000) {  // clock is set
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_yday != today) {
      today = t.tm_yday;
      p.visitsToday = 0;
    }
  }
  p.visitsToday++;
}

void presenceLoop() {
  const RadarState& r = radar();
  uint32_t now = millis();
  if (r.detected) p.lastSeenMs = now;

  bool want = r.detected || (p.present && now - p.lastSeenMs < settings.holdSeconds * 1000UL);
  if (want == p.present) return;

  uint32_t lasted = (now - p.sinceMs) / 1000;
  p.present = want;
  p.sinceMs = now;
  digitalWrite(PIN_LED, want);

  if (want) {
    countVisit();
    logEvent("present", r.uart ? String(r.moving ? r.movingCm : r.stillCm) + " cm" : "");
  } else {
    logEvent("clear", "after " + String(lasted) + " s");
  }
}

const Presence& presence() { return p; }
