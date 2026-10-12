#include "storage.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <SD_MMC.h>
#endif

#include "hwconfig.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
// A card on the same SPI bus as the LCD (Waveshare ESP32-C6-LCD-1.47) goes through ESP-IDF's
// SD-over-SPI driver, which shares the bus with LovyanGFX (bus_shared) by taking turns.
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <vfs_api.h>
#define HAS_SHARED_SD 1

class SharedCardFs : public fs::FS {
 public:
  SharedCardFs() : fs::FS(FSImplPtr(new VFSImpl())) {}
  void mountedAt(const char* mp) { _impl->mountpoint(mp); }
};
static SharedCardFs sharedCard;
static sdmmc_card_t* sharedCardHandle = nullptr;
static const char* SHARED_MOUNT = "/sd";
#else
#define HAS_SHARED_SD 0
#endif

enum Mount { MOUNT_NONE, MOUNT_SPI, MOUNT_MMC, MOUNT_SHARED };
static Mount card = MOUNT_NONE;
static bool sdConfigured = false;
static bool flashOk = false;
static String sdError;
static uint32_t lastCheck = 0;

static fs::FS& cardFs() {
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) return SD_MMC;
#endif
#if HAS_SHARED_SD
  if (card == MOUNT_SHARED) return sharedCard;
#endif
  return SD;
}

// The SD card's SPI bus: its own, unless it shares the LCD's pins. Arduino's `SPI` is left to
// the W5500 / camera, so a card on other pins gets the chip's second SPI controller.
#if SOC_SPI_PERIPH_NUM > 2
static SPIClass cardSpi(HSPI);
#define CARD_SPI cardSpi
#else
#define CARD_SPI SPI  // the ESP32-C3 / C6 have one SPI controller for everything
#endif

bool storageCardSharesDisplay() {
  JsonVariantConst sd = hw["sd"], d = hw["display"];
  return !strcmp(sd["type"] | "", "spi") && !strcmp(d["type"] | "", "st7789") &&
         hwPin(sd, "sck") == hwPin(d, "sclk") && hwPin(sd, "mosi") == hwPin(d, "mosi");
}

#if HAS_SHARED_SD
static bool mountShared(JsonVariantConst sd) {
  // The LCD driver has set up SPI2 with the card's MISO as well (see gfx_lcd.cpp)
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = (sd["spiMhz"] | 20) * 1000;
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = (gpio_num_t)hwPin(sd, "cs");
  dev.host_id = SPI2_HOST;
  esp_vfs_fat_mount_config_t mount = {};
  mount.format_if_mount_failed = false;
  mount.max_files = 5;
  mount.allocation_unit_size = 16 * 1024;
  esp_err_t err = esp_vfs_fat_sdspi_mount(SHARED_MOUNT, &host, &dev, &mount, &sharedCardHandle);
  if (err != ESP_OK) {
    sdError = String("no card (") + esp_err_to_name(err) + ")";
    return false;
  }
  sharedCard.mountedAt(SHARED_MOUNT);
  card = MOUNT_SHARED;
  return true;
}
#endif

static bool mountCard() {
  JsonVariantConst sd = hw["sd"];
  String type = sd["type"] | "";
  if (type == "spi") {
    if (storageCardSharesDisplay()) {
#if HAS_SHARED_SD
      return mountShared(sd);
#else
      sdError = "sharing the LCD's bus needs the Arduino core 3 firmware";
      return false;
#endif
    }
    CARD_SPI.begin(hwPin(sd, "sck"), hwPin(sd, "miso"), hwPin(sd, "mosi"));
    if (SD.begin(hwPin(sd, "cs"), CARD_SPI, (sd["spiMhz"] | 20) * 1000000)) { card = MOUNT_SPI; return true; }
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

#if HAS_SHARED_SD
static void sharedInfo(uint64_t& total, uint64_t& free) {
  total = free = 0;
  esp_vfs_fat_info(SHARED_MOUNT, &total, &free);
}
#endif

uint64_t storageTotal() {
#if HAS_SHARED_SD
  if (card == MOUNT_SHARED) { uint64_t t, f; sharedInfo(t, f); return t; }
#endif
  if (card == MOUNT_SPI) return SD.totalBytes();
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) return SD_MMC.totalBytes();
#endif
  return flashOk ? LittleFS.totalBytes() : 0;
}

uint64_t storageFree() {
#if HAS_SHARED_SD
  if (card == MOUNT_SHARED) { uint64_t t, f; sharedInfo(t, f); return f; }
#endif
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
#if HAS_SHARED_SD
  if (card == MOUNT_SHARED) esp_vfs_fat_sdcard_unmount(SHARED_MOUNT, sharedCardHandle);
#endif
#if SOC_SDMMC_HOST_SUPPORTED
  if (card == MOUNT_MMC) SD_MMC.end();
#endif
  card = MOUNT_NONE;
  sdError = "card removed";
  Serial.println("storage: microSD card went away, using flash");
}
