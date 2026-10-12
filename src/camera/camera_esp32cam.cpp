// AI-Thinker ESP32-CAM: OV2640 on the board's fixed pins, through the esp32-camera component
// bundled with the Arduino core (esp_camera.h). Config: {"type":"esp32cam"}. The sensor does
// the JPEG itself; the frame buffer goes to PSRAM when the board has it (it should), else DRAM
// (fine for QVGA). SCCB uses I2C port 1 on GPIO26/27, so Wire (port 0) stays free; XCLK uses
// LEDC timer 0 / channel 0.
#include "camera.h"

#if CONFIG_IDF_TARGET_ESP32 && __has_include(<esp_camera.h>)
#include <esp_camera.h>

namespace {

class Esp32Cam : public CameraBackend {
  bool inited = false;

 public:
  bool begin(JsonVariantConst cfg, CameraSize size) override {
    camera_config_t c = {};
    c.pin_pwdn = 32;
    c.pin_reset = -1;
    c.pin_xclk = 0;
    c.pin_sccb_sda = 26;
    c.pin_sccb_scl = 27;
    c.pin_d7 = 35;
    c.pin_d6 = 34;
    c.pin_d5 = 39;
    c.pin_d4 = 36;
    c.pin_d3 = 21;
    c.pin_d2 = 19;
    c.pin_d1 = 18;
    c.pin_d0 = 5;
    c.pin_vsync = 25;
    c.pin_href = 23;
    c.pin_pclk = 22;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size = size == CAM_VGA ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
    c.jpeg_quality = 12;  // 0-63, lower is better
    c.fb_count = 1;
    c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    c.sccb_i2c_port = 1;
    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) { error = String("esp_camera_init: ") + esp_err_to_name(err); return false; }
    inited = true;
    sensor_t* s = esp_camera_sensor_get();
    if (s) s->set_vflip(s, 0);
    // a few frames so exposure and white balance settle
    for (int i = 0; i < 3; i++) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) esp_camera_fb_return(fb);
    }
    return true;
  }

  size_t capture(Print& out) override {
    if (!inited) return 0;
    // the first frame can be stale; take two and keep the second
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb) { error = "no frame"; return 0; }
    size_t n = out.write(fb->buf, fb->len);
    size_t len = fb->len;
    esp_camera_fb_return(fb);
    if (n != len) { error = "write failed"; return 0; }
    error = "";
    return len;
  }

  String name() override { return "ESP32-CAM OV2640"; }
};

}  // namespace

CameraBackend* makeEsp32Cam() { return new Esp32Cam(); }

#else

CameraBackend* makeEsp32Cam() { return nullptr; }

#endif
