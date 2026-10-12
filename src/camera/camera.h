// Camera: takes a JPEG when a rule or the web page asks for one and keeps the newest few in
// storage (microSD or flash, see storage.h) under /photos (index in /photos/index.txt). Configured by hw["camera"]:
//
//   {"type":"arducam_mega","cs":5,"sck":18,"mosi":23,"miso":19}
//       Arducam Mega 3MP/5MP SPI camera (Arducam_Mega library) on the Arduino SPI bus
//       (VSPI on the ESP32, FSPI elsewhere). Optional "spiMhz" (default 8).
//   {"type":"esp32cam"}
//       The OV2640 of an AI-Thinker ESP32-CAM on its fixed pins (esp32-camera). ESP32 only.
//   {"type":"p4_csi","sensor":"auto","sda":7,"scl":8}
//       ESP32-P4 MIPI-CSI connector (Waveshare ESP32-P4-ETH / -Module-DEV-KIT), SCCB on the
//       given I2C pins (default GPIO7/8; shares the Wire bus when hw["i2c"] is on the same pins).
//       "sensor": "auto" (default), "ov5647" or "sc2336"; the driver detects the fitted one
//       anyway, a mismatch only shows in the status line. ESP32-P4 only.
//   Every type takes an optional "keep": how many photos to keep (default 3 on 4 MB boards,
//   20 on the 16 MB ESP32-S3 / ESP32-P4 builds) and "size": "qvga" (320x240) or "vga"
//   (640x480; default on the 16 MB builds, qvga on the 4 MB ones).
// Without "camera" nothing is set up and cameraStatus() is "none".
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void cameraBegin();   // reads hw["camera"]; no camera -> does nothing
bool cameraPresent();
String cameraStatus();  // one line: "Arducam Mega 5MP, 640x480, 3 photos" / "not found" / "none"

// Takes a JPEG and stores it; returns the file path ("/photos/0001.jpg") or "" on failure.
// Keeps at most `keep` newest photos in the photo dir (deletes the oldest).
String cameraCapture(const String& reason);

// Stored photos, for the web page / log
struct PhotoInfo {
  String path;
  uint32_t time;  // unix time of the capture (0 if the clock wasn't set)
  String reason;
  size_t bytes;
};
int cameraPhotoCount();
bool cameraPhotoAt(int i, PhotoInfo& out);  // newest first
// Streams the raw JPEG of a stored photo to a Print (WebServer client); false if missing.
bool cameraSendPhoto(const String& path, Print& to);

// --- internal: one backend per camera type (camera_*.cpp) ---------------------------------
enum CameraSize { CAM_QVGA, CAM_VGA };

class CameraBackend {
 public:
  virtual ~CameraBackend() {}
  // false if the camera didn't answer; error() then says why
  virtual bool begin(JsonVariantConst cfg, CameraSize size) = 0;
  // Takes one JPEG and writes it to `out`. Returns the byte count, 0 on failure.
  virtual size_t capture(Print& out) = 0;
  virtual String name() = 0;  // "Arducam Mega 5MP"
  String error;               // why begin() or the last capture() failed
};

CameraBackend* makeArducamMega();
CameraBackend* makeEsp32Cam();  // AI-Thinker ESP32-CAM, nullptr on other chips
CameraBackend* makeP4Csi();     // ESP32-P4 MIPI-CSI, nullptr on other chips
