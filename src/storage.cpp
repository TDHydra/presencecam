#include "storage.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <SD_MMC.h>
#endif

#include "hwconfig.h"

enum Mount { MOUNT_NONE, MOUNT_SPI, MOUNT_MMC };
static Mount card = MOUNT_NONE;
static bool sdConfigured = false;
static bool flashOk = false;
static String sdError;
static uint32_t lastCheck = 0;

static fs::FS& cardFs() {
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) return SD_MMC;
#endif
  return SD;
}

static bool mountCard() {
  JsonVariantConst sd = hw["sd"];
  String type = sd["type"] | "";
  if (type == "spi") {
    // SPI is the Arduino default bus; the camera or W5500 may already have started it on the
    // same pins, and begin() again with the same pins is harmless.
    SPI.begin(hwPin(sd, "sck"), hwPin(sd, "miso"), hwPin(sd, "mosi"));
    if (SD.begin(hwPin(sd, "cs"), SPI, (sd["spiMhz"] | 20) * 1000000)) { card = MOUNT_SPI; return true; }
    sdError = "no card (or not answering on SPI)";
    return false;
  }
#if SOC_SDMMC_HOST_SUPPORTED
  if (type == "sdmmc") {
    int power = hwPin(sd, "power");
    if (power >= 0) {
      pinMode(power, OUTPUT);
      digitalWrite(power, sd["powerLevel"] | 0);
      delay(10);
    }
    bool oneBit = (sd["width"] | 4) == 1;
    bool pinsOk = oneBit ? SD_MMC.setPins(hwPin(sd, "clk"), hwPin(sd, "cmd"), hwPin(sd, "d0"))
                         : SD_MMC.setPins(hwPin(sd, "clk"), hwPin(sd, "cmd"), hwPin(sd, "d0"), hwPin(sd, "d1"),
                                          hwPin(sd, "d2"), hwPin(sd, "d3"));
    if (!pinsOk) { sdError = "SD/MMC pins not accepted"; return false; }
    if (SD_MMC.begin("/sdcard", oneBit)) { card = MOUNT_MMC; return true; }
    sdError = "no card";
    return false;
  }
#endif
  sdError = "unsupported type " + type;
  return false;
}

void storageBegin() {
  static bool done = false;
  if (done) return;
  done = true;
  flashOk = LittleFS.begin(true);  // formats the partition the first time
  if (!flashOk) Serial.println("storage: LittleFS mount failed");
  sdConfigured = !hw["sd"].isNull();
  if (sdConfigured) {
    if (mountCard()) Serial.println("storage: " + storageDescribe());
    else Serial.println("storage: microSD " + sdError + ", using flash");
  }
}

fs::FS& storageFs() {
  storageBegin();  // callers that run before setup() reaches storageBegin() still get a mounted fs
  if (card != MOUNT_NONE) return cardFs();
  return LittleFS;
}

bool storageOnSd() { return card != MOUNT_NONE; }

uint64_t storageTotal() {
  if (card == MOUNT_SPI) return SD.totalBytes();
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) return SD_MMC.totalBytes();
#endif
  return flashOk ? LittleFS.totalBytes() : 0;
}

uint64_t storageFree() {
  if (card == MOUNT_SPI) return SD.totalBytes() - SD.usedBytes();
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) return SD_MMC.totalBytes() - SD_MMC.usedBytes();
#endif
  return flashOk ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0;
}

static String size(uint64_t b) {
  if (b >= 1000000000ULL) return String(b / 1e9, 1) + " GB";
  if (b >= 1000000ULL) return String(b / 1e6, 1) + " MB";
  return String((uint32_t)(b / 1024)) + " KB";
}

String storageDescribe() {
  String s;
  if (card != MOUNT_NONE) s = "microSD " + size(storageTotal()) + ", " + size(storageFree()) + " free";
  else if (flashOk) s = "flash " + size(storageTotal()) + ", " + size(storageFree()) + " free";
  else s = "none (flash not mounted)";
  if (sdConfigured && card == MOUNT_NONE) s += " · microSD: " + sdError;
  return s;
}

void storageLoop() {
  if (card == MOUNT_NONE || millis() - lastCheck < 5000) return;
  lastCheck = millis();
  // A removed card makes this open fail; fall back to flash until the next restart.
  File root = cardFs().open("/");
  bool alive = (bool)root;
  root.close();
  if (alive) return;
  if (card == MOUNT_SPI) SD.end();
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) SD_MMC.end();
#endif
  card = MOUNT_NONE;
  sdError = "card removed";
  Serial.println("storage: microSD card went away, using flash");
}
