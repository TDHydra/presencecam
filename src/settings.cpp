#include "settings.h"

#include <Preferences.h>

#include "config.h"

Settings settings;
static Preferences prefs;

String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[5];
  snprintf(buf, sizeof buf, "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return buf;
}

void settingsLoad() {
  prefs.begin("pcam", true);
  settings.name = prefs.getString("name", "PresenceCam-" + deviceId());
  settings.holdSeconds = prefs.getUShort("hold", DEFAULT_HOLD_S);
  settings.maxCm = prefs.getUShort("maxcm", DEFAULT_MAX_CM);
  settings.oledSleepS = prefs.getUShort("oledsleep", DEFAULT_OLED_SLEEP_S);
  settings.oledDriver = prefs.getUChar("oleddrv", 0);
  settings.oledFlip = prefs.getBool("oledflip", false);
  settings.wifiSsid = prefs.getString("ssid", "");
  settings.wifiPass = prefs.getString("pass", "");
  prefs.end();
}

void settingsSave() {
  prefs.begin("pcam", false);
  prefs.putString("name", settings.name);
  prefs.putUShort("hold", settings.holdSeconds);
  prefs.putUShort("maxcm", settings.maxCm);
  prefs.putUShort("oledsleep", settings.oledSleepS);
  prefs.putUChar("oleddrv", settings.oledDriver);
  prefs.putBool("oledflip", settings.oledFlip);
  prefs.putString("ssid", settings.wifiSsid);
  prefs.putString("pass", settings.wifiPass);
  prefs.end();
}

void settingsForgetWifi() {
  settings.wifiSsid = "";
  settings.wifiPass = "";
  settingsSave();
}
