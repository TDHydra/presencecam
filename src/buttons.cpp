#include "buttons.h"
#include "debuglog.h"

#include <Preferences.h>
#include <esp_system.h>

#include "display.h"
#include "hwconfig.h"
#include "net.h"
#include "settings.h"

static const uint32_t TAP_WINDOW_MS = 3000;
static const uint32_t DOUBLE_GAP_MS = 350;
static const uint32_t HOLD1_MS = 3000;
static const uint32_t HOLD2_MS = 10000;

static int pin = 0;
static bool tapsCleared = false;

// --- RST taps -------------------------------------------------------------------------

int buttonsCountResetTaps() {
  esp_reset_reason_t why = esp_reset_reason();
  Preferences p;
  p.begin("pcamtap", false);
  int stored = p.getUChar("n", 0);
  // Only a reset from the RST/EN pin or power counts as a tap (a crash or "reboot" doesn't).
  int taps = (why == ESP_RST_POWERON || why == ESP_RST_EXT) ? stored + 1 : 1;
  // Skip the write when nothing changes, so a reset loop doesn't wear the flash
  if (min(taps, 9) != stored) p.putUChar("n", min(taps, 9));
  p.end();
  return taps;
}

static void clearTapsAfterWindow() {
  if (tapsCleared || millis() < TAP_WINDOW_MS) return;
  tapsCleared = true;
  Preferences p;
  p.begin("pcamtap", false);
  p.putUChar("n", 0);
  p.end();
}

// --- BOOT gestures --------------------------------------------------------------------

static String action(const char* gesture, const char* fallback) {
  return hw["buttons"][gesture] | fallback;
}

static const char* actionLabel(const String& a) {
  if (a == "next") return "next page";
  if (a == "dim") return "dim screen";
  if (a == "info") return "show address";
  if (a == "forget_wifi") return "forget Wi-Fi";
  if (a == "setup_ap") return "setup network";
  if (a == "reboot") return "restart";
  return "nothing";
}

static void run(const String& a) {
  Serial.println("button: " + a);
  if (a == "next") displayNextPage();
  else if (a == "dim") displayToggleDim();
  else if (a == "info") displayShowInfo(30000);
  else if (a == "setup_ap") netStartSetupAp(10 * 60 * 1000);
  else if (a == "reboot") ESP.restart();
  else if (a == "forget_wifi") {
    displayMessage("Wi-Fi forgotten", "restarting...", 3000);
    settingsForgetWifi();
    uint32_t t = millis();
    while (millis() - t < 1500) displayLoop();
    ESP.restart();
  }
}

void buttonsBegin() {
  pin = hwPin(hw.as<JsonVariantConst>(), "button", 0);
  if (pin >= 0) pinMode(pin, INPUT_PULLUP);
}

void buttonsLoop() {
  clearTapsAfterWindow();
  if (pin < 0) return;

  static bool was = false;
  static uint32_t downAt = 0, upAt = 0;
  static int clicks = 0;
  uint32_t now = millis();
  bool down = digitalRead(pin) == LOW;

  if (down && !was) {
    downAt = now;
    displayWake();
  }
  if (down) {
    uint32_t held = now - downAt;
    if (held > 600) {  // show what releasing now would do
      String h1 = action("hold3", "info"), h2 = action("hold10", "forget_wifi");
      if (held < HOLD1_MS) displayHoldHint(held, actionLabel(action("short", "next")), actionLabel(h1), HOLD1_MS);
      else if (held < HOLD2_MS) displayHoldHint(held - HOLD1_MS, actionLabel(h1), actionLabel(h2), HOLD2_MS - HOLD1_MS);
      else displayHoldHint(held, actionLabel(h2), nullptr, 0);
    }
  }
  if (!down && was) {
    uint32_t held = now - downAt;
    displayHoldHintClear();
    if (held >= HOLD2_MS) { clicks = 0; run(action("hold10", "forget_wifi")); }
    else if (held >= HOLD1_MS) { clicks = 0; run(action("hold3", "info")); }
    else if (held > 30) { clicks++; upAt = now; }
  }
  // Decide between a single and a double press once the gap has passed
  if (!down && clicks && now - upAt > DOUBLE_GAP_MS) {
    run(clicks >= 2 ? action("double", "dim") : action("short", "next"));
    clicks = 0;
  }
  was = down;
}
