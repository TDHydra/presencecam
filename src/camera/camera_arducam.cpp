// Arducam Mega 3MP / 5MP SPI camera (Arducam_Mega library, lib_deps dennis-ard/Arducam_Mega).
// Config: {"type":"arducam_mega","cs","sck","mosi","miso","spiMhz"}. The library talks to the
// Arduino `SPI` object and calls SPI.begin() without pins, which is a no-op once the bus is
// up, so the pins are set here first. The camera returns the JPEG from its FIFO, which is
// streamed straight into the file.
#include "camera.h"

#include "../hwconfig.h"

#if __has_include(<Arducam_Mega.h>)
#include <Arducam_Mega.h>
#include <SPI.h>

namespace {

class ArducamMega : public CameraBackend {
  Arducam_Mega* cam = nullptr;
  uint8_t id = 0;
  CAM_IMAGE_MODE mode = CAM_IMAGE_MODE_VGA;

 public:
  bool begin(JsonVariantConst cfg, CameraSize size) override {
    int cs = hwPin(cfg, "cs"), sck = hwPin(cfg, "sck"), mosi = hwPin(cfg, "mosi"), miso = hwPin(cfg, "miso");
    if (cs < 0 || sck < 0 || mosi < 0 || miso < 0) { error = "cs/sck/mosi/miso pins missing"; return false; }
    mode = size == CAM_VGA ? CAM_IMAGE_MODE_VGA : CAM_IMAGE_MODE_QVGA;
    SPI.begin(sck, miso, mosi, -1);
    SPI.setFrequency((cfg["spiMhz"] | 8) * 1000000UL);
    cam = new Arducam_Mega(cs);
    cam->begin();
    id = cam->getCameraInstance()->cameraId;  // 0x81..0x84 when a Mega answers
    if (id < SENSOR_5MP_1 || id > SENSOR_3MP_2) {
      char hex[32];
      snprintf(hex, sizeof hex, "no answer on SPI (id 0x%02x)", id);
      error = hex;
      return false;
    }
    cam->setImageQuality(DEFAULT_QUALITY);
    return true;
  }

  size_t capture(Print& out) override {
    // takePicture() spins until the camera reports the frame, so make sure it still answers
    if (!cameraHeartBeat(cam->getCameraInstance())) { error = "camera not responding"; return 0; }
    if (cam->takePicture(mode, CAM_IMAGE_PIX_FMT_JPG) != CAM_ERR_SUCCESS) {
      error = "takePicture failed";
      return 0;
    }
    uint32_t total = cam->getTotalLength();
    if (total == 0 || total > 2000000) { error = "bad image length " + String(total); return 0; }
    uint8_t buf[1024];
    size_t written = 0;
    while (cam->getReceivedLength() > 0) {
      uint32_t n = readBuff(cam->getCameraInstance(), buf, sizeof buf);  // C API: chunks > 255
      if (!n) break;
      if (out.write(buf, n) != n) { error = "write failed"; return 0; }
      written += n;
    }
    error = "";
    return written;
  }

  String name() override {
    return String("Arducam Mega ") + (id == SENSOR_3MP_1 || id == SENSOR_3MP_2 ? "3MP" : "5MP");
  }
};

}  // namespace

CameraBackend* makeArducamMega() { return new ArducamMega(); }

#else  // library not installed: add dennis-ard/Arducam_Mega to lib_deps

CameraBackend* makeArducamMega() { return nullptr; }

#endif
